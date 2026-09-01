#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Full local pipeline: find functions with no fuzz coverage, turn them
into candidate files, and have Claude (via OSS-Fuzz-Gen) draft a
fuzz_test_file for a batch of them. Deliberately NOT a CI job -- see
README.md in this directory. Gap analysis costs nothing, but drafting
spends real GCP Vertex AI dollars per candidate, so both stay
occasional/manual rather than recurring.

Cost control: shares fuzz_spend_ledger.py's monthly cap with
fuzz_script_generate_pool.py (see that module's own docstring for why
it's one shared cap, not one per script). Unlike that script, OSS-Fuzz-Gen
is an external tool we don't instrument internally -- there's no real
per-call usage to read the way claude_model.py reads it from the
Anthropic API response, so the budget check here is a documented
ESTIMATE (see EST_COST_PER_CANDIDATE_USD below), not a measurement.
--draft-limit is still honored as an independent upper bound if you want
one; the estimated remaining budget is whichever is smaller.

The budget is checked once up front and spent once after the whole
batch, exactly like the bash version this replaced -- candidates draft
concurrently (--concurrency, default 3, since each is an independent
OSS-Fuzz-Gen subprocess and drafting one can take several minutes of
fix-iteration rounds), but that single check-then-spend bracket is what
keeps the concurrency from needing its own locking around the shared
ledger: nothing reads or writes fuzz_spend_ledger.py's state file while
candidates are drafting, only before and after.

A failure in one candidate's draft no longer aborts the whole run (it
did under the old bash `set -eu` loop) -- it's reported and the other
candidates in this run's batch still complete, since they're already
running concurrently and unrelated to each other.

PREREQUISITE for drafting (not for gap analysis): GCP Vertex AI access.
Authenticate however your gcloud/ADC setup normally works before running
this with --draft-limit > 0 -- see README.md's two confirmed
requirements (Vertex AI only, no bare ANTHROPIC_API_KEY path; current
model registration via fuzz_code_vertex_claude_patch.py).

Usage:
  ./fuzz_code_generate_and_draft.py <path-to-tapyrus-core-checkout> [options]
"""
import argparse
import asyncio
import json
import shutil
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
POOL_DIR = SCRIPT_DIR / "candidate_pool"
FUZZ_DIR = SCRIPT_DIR.parent.parent  # contrib/fuzz/ -- two levels up from oss-fuzz/drafting/
LEDGER_DIR = FUZZ_DIR
INTROSPECTOR_DIR = FUZZ_DIR / "fuzz-introspector"

sys.path.insert(0, str(LEDGER_DIR))
import fuzz_spend_ledger  # noqa: E402
from _async_proc import Command  # noqa: E402
sys.path.insert(0, str(INTROSPECTOR_DIR))
from fuzz_code_run_introspector import IntrospectorRunner  # noqa: E402

# Rough estimate, not a measurement (see this file's own module
# docstring): ~12 LLM round-trips per candidate at NUM_SAMPLES=2/
# MAX_ROUND=5 (2 initial drafts + fix-iteration rounds), ~5K input + ~1K
# output tokens/call, claude-opus-5 pricing ($5/$25 per MTok) -- roughly
# $0.60/candidate. Recompute this comment if NUM_SAMPLES/MAX_ROUND
# defaults change materially.
EST_COST_PER_CANDIDATE_USD = 0.60


class DraftPipeline:
    def __init__(self, tapyrus_core_src: Path, *, introspector_seconds: int = 30,
                 gap_candidate_limit: int = 100, draft_limit: int = 100000,
                 num_samples: int = 2, max_round: int = 5, concurrency: int = 3):
        self.tapyrus_core_src = tapyrus_core_src
        self.introspector_seconds = introspector_seconds
        self.gap_candidate_limit = gap_candidate_limit
        self.draft_limit = draft_limit
        self.num_samples = num_samples
        self.max_round = max_round
        self.concurrency = concurrency
        self.work_dir = Path(tempfile.mkdtemp())

    async def run(self) -> int:
        try:
            return await self._run()
        finally:
            shutil.rmtree(self.work_dir, ignore_errors=True)

    async def _run(self) -> int:
        print("== 1/4: Fuzz Introspector gap analysis ==")
        report_dir = await IntrospectorRunner(self.tapyrus_core_src, self.introspector_seconds).run()
        gaps_path = self.work_dir / "fuzz_gaps.json"
        await Command(
            "python3", INTROSPECTOR_DIR / "fuzz_code_find_fuzz_gaps.py",
            report_dir, "--out", gaps_path,
        ).run()
        candidates_raw = json.loads(gaps_path.read_text())
        print(f"{len(candidates_raw)} uncovered untrusted-input function(s) found.")
        if not candidates_raw:
            print("Nothing to do.")
            return 0

        print("== 2/4: generate candidate files ==")
        await Command(sys.executable, "-m", "pip", "install", "-q", "pyyaml").run()
        await Command(
            "python3", SCRIPT_DIR / "fuzz_code_generate_candidates.py", gaps_path,
            "--out-dir", POOL_DIR, "--limit", str(self.gap_candidate_limit),
        ).run()
        print(f"Review new entries under {POOL_DIR} and commit the ones worth keeping.")

        if self.draft_limit == 0:
            print("Skipping drafting (--draft-limit 0). Done.")
            return 0

        remaining = fuzz_spend_ledger.remaining_budget()
        print(f"Shared monthly fuzz-generation budget remaining: ${remaining:.4f} (estimate for this pipeline)")
        affordable = int(remaining / EST_COST_PER_CANDIDATE_USD)
        draft_count = min(affordable, self.draft_limit)
        if draft_count <= 0:
            print("No shared budget remains for drafting this month (or --draft-limit 0). Skipping step 3.")
            return 0

        print(f"== 3/4: draft fuzz_test_files for up to {draft_count} candidates (budget estimate) ==")
        candidate_yamls = sorted(POOL_DIR.glob("*.yaml"))[:draft_count]
        if not candidate_yamls:
            print(f"No candidate YAMLs in {POOL_DIR} to draft. Done.")
            return 0

        oss_fuzz_gen_dir = self.work_dir / "oss-fuzz-gen"
        await Command(
            "git", "clone", "--depth=1",
            "https://github.com/google/oss-fuzz-gen.git", oss_fuzz_gen_dir,
        ).run()
        patch_src = (SCRIPT_DIR / "fuzz_code_vertex_claude_patch.py").read_text()
        with (oss_fuzz_gen_dir / "llm_toolkit" / "models.py").open("a") as models_file:
            models_file.write(patch_src)

        draft_out_dir = SCRIPT_DIR / f"local_drafts_{datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S')}"
        draft_out_dir.mkdir(parents=True, exist_ok=True)

        drafted_count = await self._draft_all(candidate_yamls, oss_fuzz_gen_dir, draft_out_dir)

        # Estimated, not measured -- see this file's own module docstring.
        # Recorded regardless of exact per-candidate variance so the
        # shared ledger stays roughly accurate for
        # fuzz_script_generate_pool.py's own (real, measured) spend to
        # react to.
        estimated_spend = drafted_count * EST_COST_PER_CANDIDATE_USD
        fuzz_spend_ledger.record_spend(estimated_spend)

        print("== 4/4: build the review page ==")
        await Command(
            "python3", SCRIPT_DIR / "fuzz_code_build_review_page.py", draft_out_dir,
            "--pool-dir", POOL_DIR,
        ).run()

        print()
        print(f"Drafted fuzz_test_files (if any) are under {draft_out_dir}")
        print(f"Estimated spend this run: ${estimated_spend:.2f} "
              f"({drafted_count} candidates x ${EST_COST_PER_CANDIDATE_USD})")
        print(f"Shared monthly budget remaining: ${fuzz_spend_ledger.remaining_budget():.4f}")
        print()
        print(f"Open {draft_out_dir}/review_code.html in a browser, check the candidates")
        print("worth keeping (edit their target name if you want a different one),")
        print("then File > Save Page As (Webpage, HTML Only) back to an .html file.")
        print("Then land the approved ones -- writes each .cpp under src/test/fuzz/,")
        print("its add_executable(...) block in src/test/CMakeLists.txt, and its name")
        print("in src/test/fuzz/FUZZ_TARGETS.txt, no git commands -- with:")
        print(f"  {SCRIPT_DIR}/fuzz_code_land_approved.py <path-to-the-saved-review_code.html>")
        return 0

    async def _draft_all(self, candidate_yamls, oss_fuzz_gen_dir: Path, draft_out_dir: Path) -> int:
        semaphore = asyncio.Semaphore(self.concurrency)

        async def draft_one(candidate_yaml: Path) -> None:
            name = candidate_yaml.stem
            results_dir = self.work_dir / f"results_{name}"
            async with semaphore:
                print(f"-- drafting {name} --")
                await Command(
                    "./run_all_experiments.py", "--model=vertex_ai_claude-opus-5",
                    "-y", candidate_yaml, f"--work-dir={results_dir}",
                    "--num-samples", str(self.num_samples), "--max-round", str(self.max_round),
                    cwd=oss_fuzz_gen_dir,
                ).run()
            fixed_targets = results_dir / "fixed_targets"
            if fixed_targets.is_dir():
                shutil.copytree(fixed_targets, draft_out_dir / name)

        results = await asyncio.gather(
            *(draft_one(c) for c in candidate_yamls), return_exceptions=True
        )
        drafted_count = 0
        for candidate_yaml, result in zip(candidate_yamls, results):
            if isinstance(result, Exception):
                print(f"-- {candidate_yaml.stem} failed: {result} --", file=sys.stderr)
            else:
                drafted_count += 1
        return drafted_count


async def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tapyrus_core_src", type=Path)
    parser.add_argument("--introspector-seconds", type=int, default=30,
                         help="Seconds Introspector spends running fuzzers during its own pass (default: 30)")
    parser.add_argument("--gap-candidate-limit", type=int, default=100,
                         help="Max new candidate files to add to the pool this run (default: 100)")
    parser.add_argument("--draft-limit", type=int, default=100000,
                         help="Upper bound on candidates to draft this run, independent of budget "
                              "(default 100000 -- high enough that the shared budget estimate, not "
                              "this, is what actually caps a run in practice; use 0 to skip drafting "
                              "and only grow the candidate pool)")
    parser.add_argument("--num-samples", type=int, default=2,
                         help="OSS-Fuzz-Gen: independent fuzz_test_file drafts per function (default: 2)")
    parser.add_argument("--max-round", type=int, default=5,
                         help="OSS-Fuzz-Gen: max fix-iteration rounds per candidate -- upstream's own "
                              "default is 100, capped here for cost control (default: 5)")
    parser.add_argument("--concurrency", type=int, default=3,
                         help="How many candidates to draft at once (default: 3)")
    args = parser.parse_args()

    pipeline = DraftPipeline(
        args.tapyrus_core_src,
        introspector_seconds=args.introspector_seconds,
        gap_candidate_limit=args.gap_candidate_limit,
        draft_limit=args.draft_limit,
        num_samples=args.num_samples,
        max_round=args.max_round,
        concurrency=args.concurrency,
    )
    return await pipeline.run()


if __name__ == "__main__":
    raise SystemExit(asyncio.run(_main()))
