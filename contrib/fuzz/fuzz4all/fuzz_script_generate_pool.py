#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Generates a batch of candidate Tapyrus Script programs via Fuzz4All +
Claude and drops them into generated_pool/tapyrus_script/, ready for
review and commit. Additive: each run adds to whatever's already there
(unique-prefixed filenames avoid collisions across runs) rather than
replacing the pool -- the intent is gradual growth over many runs, not a
single one-shot batch.

A local, human-run script -- generation is a judgment call about spend
and timing, run occasionally rather than on a fixed CI schedule. Review
the output and commit what's worth keeping. daily-test.yml's
fuzz-script-sweep job replays whatever's already committed to the pool
against a local build, on its own daily schedule, without ever calling
Claude.

Cost control: this run stops once fuzz_spend_ledger.py's shared monthly
cap is spent -- shared with fuzz_code_generate_and_draft.py (see that
module's own docstring for why it's one cap, not one per script),
enforced by claude_model.py on REAL per-call token usage from the API
response, not an estimate. There's no per-run --budget-usd flag anymore:
the cap is a single constant in fuzz_spend_ledger.py, so there's one
obvious place to change it rather than a flag someone could pass
differently on each run and lose track of. Model alternates between
Haiku 4.5 and Sonnet 5 each run (state kept in .last_model next to this
script) rather than a fixed model, so quality/cost characteristics vary
across the accumulated pool instead of leaning entirely on one tier.

PREREQUISITE: tapyrus-verify (src/tapyrus-verify.cpp), the binary this
script's target plugin (TAPYRUSSCRIPT.py) shells out to via
VERIFY_BINARY_PATH, must be built -- Fuzz4All calls it internally,
per-candidate, as part of its own generation loop (see
TAPYRUSSCRIPT.py's validate_individual), not just later in CI. Built
below via -DBUILD_SCRIPT_VERIFY=ON -DBUILD_FUZZ_TEST=ON, concurrently
with cloning Fuzz4All -- the two don't depend on each other until both
are needed to actually run it, so running them concurrently is real
wall-clock savings, not asyncio for its own sake.

Usage:
  ANTHROPIC_API_KEY=... ./fuzz_script_generate_pool.py [options]
"""
import argparse
import asyncio
import os
import shutil
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent.parent
LEDGER_DIR = SCRIPT_DIR.parent

sys.path.insert(0, str(LEDGER_DIR))
import fuzz_spend_ledger  # noqa: E402
from _async_proc import Command  # noqa: E402


class ScriptPoolGenerator:
    def __init__(self, *, force_model: str = "", max_candidates: int = 100000):
        self.force_model = force_model
        self.max_candidates = max_candidates
        self.work_dir = Path(tempfile.mkdtemp())
        self.last_model_file = SCRIPT_DIR / ".last_model"

    async def run(self) -> int:
        try:
            return await self._run()
        finally:
            shutil.rmtree(self.work_dir, ignore_errors=True)

    def _pick_model(self) -> str:
        if self.force_model:
            return self.force_model
        previous = self.last_model_file.read_text().strip() if self.last_model_file.exists() else ""
        return "claude-sonnet-5" if previous == "claude-haiku-4-5" else "claude-haiku-4-5"

    async def _build_verify_binary(self) -> Path:
        build_dir = self.work_dir / "build"
        await Command(
            "cmake", "-S", REPO_ROOT, "-B", build_dir,
            "-DBUILD_DAEMON=OFF", "-DBUILD_GUI=OFF", "-DBUILD_CLI=OFF", "-DBUILD_GENESIS=OFF",
            "-DENABLE_WALLET=OFF", "-DENABLE_TESTS=OFF", "-DENABLE_BENCH=OFF", "-DENABLE_ZMQ=OFF",
            "-DBUILD_SCRIPT_VERIFY=ON", "-DBUILD_FUZZ_TEST=ON",
        ).run()
        await Command(
            "cmake", "--build", build_dir, "--target", "tapyrus-verify",
            "-j", str(os.cpu_count() or 1),
        ).run()
        return build_dir / "src" / "tapyrus-verify"

    async def _prepare_fuzz4all(self, model: str) -> Path:
        fuzz4all_dir = self.work_dir / "fuzz4all"
        await Command(
            "git", "clone", "--depth=1",
            "https://github.com/fuzz4all/fuzz4all.git", fuzz4all_dir,
        ).run()
        shutil.copy(SCRIPT_DIR / "claude_model.py", fuzz4all_dir / "claude_model.py")
        shutil.copy(LEDGER_DIR / "fuzz_spend_ledger.py", fuzz4all_dir / "fuzz_spend_ledger.py")

        target_dir = fuzz4all_dir / "Fuzz4All" / "target" / "TAPYRUSSCRIPT"
        target_dir.mkdir(parents=True, exist_ok=True)
        # VERIFY_BINARY_PATH gets filled in once the concurrent build
        # finishes too -- see _finish_fuzz4all_prep below. This half of
        # the clone/prep doesn't need the binary itself, only its
        # eventual path.
        shutil.copy(SCRIPT_DIR / "TAPYRUSSCRIPT.py", target_dir / "TAPYRUSSCRIPT.py")

        config_text = (SCRIPT_DIR / "tapyrus_script.yaml").read_text().replace(
            "claude/claude-opus-5", f"claude/{model}"
        )
        (fuzz4all_dir / "config" / "tapyrus_script.yaml").write_text(config_text)
        return fuzz4all_dir

    @staticmethod
    def _finish_fuzz4all_prep(fuzz4all_dir: Path, verify_binary: Path) -> None:
        target_file = fuzz4all_dir / "Fuzz4All" / "target" / "TAPYRUSSCRIPT" / "TAPYRUSSCRIPT.py"
        target_file.write_text(
            target_file.read_text().replace("/TODO/build_fuzz/bin/tapyrus-verify", str(verify_binary))
        )

    async def _run(self) -> int:
        if not os.environ.get("ANTHROPIC_API_KEY"):
            print("error: ANTHROPIC_API_KEY is not set", file=sys.stderr)
            return 1

        # The shared ledger's state file needs a stable, persistent path --
        # not wherever claude_model.py happens to be running from (it gets
        # copied into a temp Fuzz4All clone below, which is deleted on
        # exit). Set as a real environment variable, not just imported
        # Python state, since claude_model.py runs inside Fuzz4All's own
        # subprocess, not this one -- os.environ mutations here are
        # inherited by every child process this script spawns from this
        # point on.
        os.environ["FUZZ_SPEND_LEDGER_STATE"] = str(LEDGER_DIR / "fuzz_spend_ledger.state")
        print(f"Shared monthly fuzz-generation budget remaining: ${fuzz_spend_ledger.remaining_budget():.4f}")

        # Alternate Haiku 4.5 / Sonnet 5 each run, unless --model overrides
        # it. claude/<model-id> selects claude_model.py's ClaudeModel (see
        # tapyrus_script.yaml); Opus 5 is deliberately not in the rotation
        # here -- it's the most expensive tier for what's a high-volume,
        # low-complexity generation task (short opcode-mnemonic snippets).
        model = self._pick_model()
        self.last_model_file.write_text(f"{model}\n")
        print(f"Using model: {model}")

        print("Building tapyrus-verify and cloning Fuzz4All concurrently...")
        verify_binary, fuzz4all_dir = await asyncio.gather(
            self._build_verify_binary(),
            self._prepare_fuzz4all(model),
        )
        self._finish_fuzz4all_prep(fuzz4all_dir, verify_binary)

        await Command("python3", SCRIPT_DIR / "fuzz_script_apply_patches.py", fuzz4all_dir).run()
        await Command(
            sys.executable, "-m", "pip", "install",
            "-r", fuzz4all_dir / "requirements.txt", "anthropic",
        ).run()

        print(f"Running Fuzz4All (up to {self.max_candidates} candidates, "
              f"stopping when the shared budget runs out)...")
        rc = await Command(
            "python3", "-m", "Fuzz4All.fuzz",
            "--config", "config/tapyrus_script.yaml",
            "--num-generations", str(self.max_candidates), "--batch-size", "1",
            cwd=fuzz4all_dir,
        ).run_allowing_failure()
        if rc != 0:
            print(f"Fuzz4All exited nonzero ({rc}) -- expected once BudgetExceededError")
            print("stops the run; treat as a real failure only if it happened immediately")
            print("(before any candidates were generated) or the traceback says otherwise.")

        run_prefix = f"{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}_{model}"
        out_dir = SCRIPT_DIR / "generated_pool" / "tapyrus_script"
        out_dir.mkdir(parents=True, exist_ok=True)
        count = 0
        outputs_dir = fuzz4all_dir / "outputs"
        if outputs_dir.is_dir():
            for f in sorted(outputs_dir.iterdir()):
                if not f.is_file():
                    continue
                shutil.copy(f, out_dir / f"{run_prefix}_{f.name}")
                count += 1

        print()
        print(f"{count} candidate(s) added to {out_dir} (prefixed {run_prefix}_)")
        print(f"Shared monthly budget remaining after this run: ${fuzz_spend_ledger.remaining_budget():.4f}")

        if count == 0:
            print("Nothing generated this run -- no review page to build.")
            return 0

        review_path = SCRIPT_DIR / f"review_scripts_{run_prefix}.html"
        await Command(
            "python3", SCRIPT_DIR / "fuzz_script_build_review_page.py", out_dir,
            "--run-prefix", run_prefix, "--out", review_path,
        ).run()

        print()
        print(f"Open {review_path} in a browser, check \"keep\" for the candidates")
        print("worth keeping, then File > Save Page As (Webpage, HTML Only) back to")
        print("an .html file. Then prune the rest -- deletes every unchecked candidate")
        print("from generated_pool/tapyrus_script/, no git commands -- with:")
        print(f"  {SCRIPT_DIR}/fuzz_script_land_approved.py <path-to-the-saved-review_scripts.html>")
        return 0


async def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model", default="",
                         help="Override model alternation and force one model for this run "
                              "(e.g. claude-haiku-4-5).")
    parser.add_argument("--max-candidates", type=int, default=100000,
                         help="Upper bound on candidates this run, independent of budget "
                              "(default 100000 -- high enough that the shared budget, not this, "
                              "is what actually stops a run in practice).")
    args = parser.parse_args()

    generator = ScriptPoolGenerator(force_model=args.model, max_candidates=args.max_candidates)
    return await generator.run()


if __name__ == "__main__":
    raise SystemExit(asyncio.run(_main()))
