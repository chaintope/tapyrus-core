#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Step 2 of 2 in the locally-run fuzz_script pipeline: runs
tapyrus-verify --fuzz (built by fuzz_script_step1_build_verify.py) on
every candidate in the src/test/fuzz/fuzz_scripts/ batch files whose
filename starts with --run-prefix, i.e. the batch Claude Code just wrote.
See fuzz_script_pool.py for the batch-file format; each candidate is run
from its own temporary single-program file.

Each candidate is classified by tapyrus-verify's own exit-code contract
(see src/tapyrus-verify.cpp's header comment):

  safe      exit 0 -- ran cleanly (a script failing verification
            normally still counts). Kept.
  rejected  exit 2 -- ParseScript rejected the text as not valid Script.
            Can never reach the interpreter, so its line (and the name
            comment directly above it) is REMOVED from the batch file,
            with the assembler's reason printed.
  crash     killed by a signal -- a real bug. Kept, so the committed
            pool reproduces it.
  failure   any other nonzero exit. Kept for a human to look at.
  timeout   ran past --timeout. Kept for a human to look at.

ASan/UBSan findings are turned into crashes: UBSAN_OPTIONS=
halt_on_error=1 and ASAN_OPTIONS=abort_on_error=1 are set unless the
caller's environment already sets them -- otherwise UBSan (built in
recoverable mode) would print a report and still exit 0, and ASan would
exit 1 on Linux, indistinguishable from a usage error.

Exits 1 if anything was classified crash/failure/timeout, 0 otherwise.
Never touches git -- `git add`/commit what's left yourself.

Usage:
  fuzz_script_step2_validate.py [--run-prefix YYYYMMDD_] [--build-dir DIR] [--timeout SECONDS]
"""
import argparse
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import List

from fuzz_script_pool import Candidate, ScriptPool

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent.parent
DEFAULT_BUILD_DIR = REPO_ROOT / "build_fuzz_verify"
REJECTED_BY_ASSEMBLER_EXIT_CODE = 2


class CandidateResult:
    SAFE = "safe"
    REJECTED = "rejected"
    CRASH = "crash"
    FAILURE = "failure"
    TIMEOUT = "timeout"

    def __init__(self, candidate: Candidate, status: str, detail: str = ""):
        self.candidate = candidate
        self.status = status
        self.detail = detail.strip()

    @property
    def needs_attention(self) -> bool:
        return self.status in (self.CRASH, self.FAILURE, self.TIMEOUT)


class CandidateValidator:
    def __init__(self, verify_binary: Path, timeout_seconds: int, work_dir: Path):
        self.verify_binary = verify_binary
        self.timeout_seconds = timeout_seconds
        self.work_dir = work_dir
        self.env = dict(os.environ)
        self.env.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
        self.env.setdefault("ASAN_OPTIONS", "abort_on_error=1")

    def validate(self, candidate: Candidate) -> CandidateResult:
        program_file = candidate.write_to(self.work_dir)
        try:
            result = subprocess.run(
                [str(self.verify_binary), "--fuzz", str(program_file)],
                capture_output=True, encoding="utf-8", errors="replace",
                timeout=self.timeout_seconds, env=self.env,
            )
        except subprocess.TimeoutExpired:
            return CandidateResult(candidate, CandidateResult.TIMEOUT,
                                   f"no exit after {self.timeout_seconds}s")

        if result.returncode == 0:
            return CandidateResult(candidate, CandidateResult.SAFE)
        if result.returncode == REJECTED_BY_ASSEMBLER_EXIT_CODE:
            return CandidateResult(candidate, CandidateResult.REJECTED, result.stderr)
        # A negative returncode means the child was killed by that signal
        # (e.g. -11 for SIGSEGV, -6 for SIGABRT from ASan/UBSan).
        if result.returncode < 0:
            return CandidateResult(candidate, CandidateResult.CRASH,
                                   f"signal {-result.returncode}\n{result.stderr}")
        return CandidateResult(candidate, CandidateResult.FAILURE,
                               f"exit {result.returncode}\n{result.stderr}")


class BatchValidator:
    def __init__(self, run_prefix: str, validator: CandidateValidator, pool: ScriptPool):
        self.run_prefix = run_prefix
        self.validator = validator
        self.pool = pool

    def run(self) -> int:
        candidates = self.pool.candidates(self.run_prefix)
        if not candidates:
            print(f"No candidates in {self.pool.pool_dir} batch files starting with "
                  f"{self.run_prefix!r}. Nothing to do.")
            return 0

        results: List[CandidateResult] = [self.validator.validate(c) for c in candidates]
        rejected = [r.candidate for r in results if r.status == CandidateResult.REJECTED]
        for batch in self.pool.batch_files(self.run_prefix):
            batch.remove(rejected)

        width = max(len(r.candidate.id) for r in results)
        for r in results:
            suffix = " (removed)" if r.status == CandidateResult.REJECTED else ""
            print(f"  {r.candidate.id:<{width}}  {r.status}{suffix}")
        for r in results:
            if r.detail and r.status != CandidateResult.SAFE:
                print(f"\n--- {r.candidate.id}: {r.status}\n{r.candidate.text}\n{r.detail}")

        counts = {s: sum(1 for r in results if r.status == s) for s in (
            CandidateResult.SAFE, CandidateResult.REJECTED, CandidateResult.CRASH,
            CandidateResult.FAILURE, CandidateResult.TIMEOUT)}
        print()
        print(", ".join(f"{n} {s}" for s, n in counts.items()))
        print("Not committed -- review what's left in the pool and `git add`/commit yourself.")
        return 1 if any(r.needs_attention for r in results) else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run-prefix", default=datetime.now(timezone.utc).strftime("%Y%m%d_"),
                         help="Only validate batch files whose name starts with this "
                              "(default: today's UTC date, YYYYMMDD_).")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR,
                         help=f"Where fuzz_script_step1_build_verify.py built tapyrus-verify "
                              f"(default: {DEFAULT_BUILD_DIR}).")
    parser.add_argument("--timeout", type=int, default=60,
                         help="Per-candidate timeout in seconds (default 60).")
    args = parser.parse_args()

    verify_binary = args.build_dir.resolve() / "bin" / "tapyrus-verify"
    if not verify_binary.is_file():
        sys.exit(f"error: {verify_binary} not found -- run fuzz_script_step1_build_verify.py first")

    with tempfile.TemporaryDirectory(prefix="fuzz_script_step2_") as work_dir:
        validator = CandidateValidator(verify_binary, args.timeout, Path(work_dir))
        return BatchValidator(args.run_prefix, validator, ScriptPool()).run()


if __name__ == "__main__":
    raise SystemExit(main())
