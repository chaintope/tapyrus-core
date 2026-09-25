#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Step 3 of 3 in the split, locally-run introspector gap-analysis
pipeline. See fuzz_code_step1_build_image.py's docstring for the overall
rationale.

Requires the persistent container from fuzz_code_step2_start_container.py
to already be running -- this script only execs into it, it never builds
an image or starts/recreates the container itself, so the three steps
stay genuinely independent and individually re-runnable. Meant to be run
often (unlike steps 1-2): re-run this any time the checkout's source has
changed and you want an updated gap list, with no rebuild/restart cost.

Runs fuzz_code_step3_analyze_incontainer.py inside the container (coverage
build + Fuzz Introspector full analysis + binary correlation), then feeds
its output through the existing fuzz_code_find_fuzz_gaps.py exactly as
before -- this step only changes how the report gets produced, not how
it's turned into a candidate gap list.

--analysis-only skips the coverage-sanitizer build inside the container
and reuses whatever binaries a prior run already left under its /out
mount (see fuzz_code_step3_analyze_incontainer.py's own docstring) --
this is the fast path for the "source changed, re-rank gaps" loop this
whole split pipeline exists for. Omit it (the default) the first time
against a freshly (re)started container, since no binaries exist yet.

Usage:
  fuzz_code_step3_analyze.py [--container NAME] [--analysis-only] [--out <fuzz_gaps.json path>]
"""
import argparse
import asyncio
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from _async_proc import Command  # noqa: E402

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_CONTAINER_NAME = "tapyrus-fuzz-introspector"
IN_CONTAINER_SCRIPT = "/src/tapyrus-core/contrib/fuzz/fuzz-introspector/fuzz_code_step3_analyze_incontainer.py"


def _container_running(name: str) -> bool:
    result = subprocess.run(
        ["docker", "inspect", "--format", "{{.State.Running}}", name],
        capture_output=True, text=True,
    )
    return result.returncode == 0 and result.stdout.strip() == "true"


def _out_mount_source(name: str) -> Path:
    """Asks Docker what host path is actually bind-mounted at /out for the
    named container, rather than guessing from a fixed, name-based
    heuristic. A previous version of this function derived the host
    output path from --container alone (a HOST_BASE_DIR/f"out_{name}"
    pattern, with a special-cased LEGACY_DEFAULT_OUT_DIR fallback for the
    original default-named container from before this pipeline's output
    directory moved outside the checkout) -- confirmed to actually break
    silently: a long-running default-named container's real /out mount
    can differ from what that heuristic assumes, so it read an empty,
    stale directory, passed the report_dir.is_dir() existence check
    below (the directory existed, just with no real content), and
    reported success with zero candidates. Querying the container's own
    mount config directly can't drift out of sync with reality this way.
    """
    result = subprocess.run(
        ["docker", "inspect", "--format",
         '{{range .Mounts}}{{if eq .Destination "/out"}}{{.Source}}{{end}}{{end}}', name],
        capture_output=True, text=True,
    )
    if result.returncode != 0 or not result.stdout.strip():
        sys.exit(f"error: could not determine {name}'s /out mount via `docker inspect` -- "
                 f"is it running? ({result.stderr.strip()})")
    return Path(result.stdout.strip())


async def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--container", default=DEFAULT_CONTAINER_NAME,
                         help=f"Container name to exec into (default: {DEFAULT_CONTAINER_NAME}) -- "
                              "match whatever --name you gave fuzz_code_step2_start_container.py.")
    parser.add_argument("--analysis-only", action="store_true",
                         help="Skip the coverage build; reuse binaries already under this "
                              "container's /out from a prior run. Fast re-rank loop -- see "
                              "this script's own module docstring.")
    parser.add_argument("--out", type=Path, default=Path("fuzz_gaps.json"))
    parser.add_argument("--undercovered-out", type=Path, default=Path("fuzz_gaps_undercovered.json"),
                         help="Second output: functions an existing harness reaches but with 0%% "
                              "measured runtime coverage -- see fuzz_code_find_fuzz_gaps.py's own "
                              "module docstring.")
    args = parser.parse_args()

    if not _container_running(args.container):
        sys.exit(f"error: {args.container} is not running -- run "
                 "fuzz_code_step2_start_container.py <path-to-tapyrus-core-checkout> "
                 f"[--name {args.container}] first")

    exec_args = ["docker", "exec", args.container, "python3", "-u", IN_CONTAINER_SCRIPT]
    if args.analysis_only:
        exec_args.append("--analysis-only")
    await Command(*exec_args).run()

    report_dir = _out_mount_source(args.container) / "inspector"
    if not report_dir.is_dir():
        sys.exit(f"error: {report_dir} was not produced -- see the analysis output above")

    await Command(
        "python3", str(SCRIPT_DIR / "fuzz_code_find_fuzz_gaps.py"),
        str(report_dir), "--out", str(args.out), "--undercovered-out", str(args.undercovered_out),
    ).run()

    print()
    print(f"Candidate gap list written to {args.out}.")
    print(f"Reached-but-0%-runtime-coverage list written to {args.undercovered_out} -- these "
          "already have a harness that can reach them; consider a richer seed corpus or a "
          "longer fuzzing run instead of drafting a new harness for these.")
    print("Next: fuzz_code_generate_candidates.py, then ask Claude Code to "
          "draft a harness from one of the resulting candidate YAMLs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(_main()))
