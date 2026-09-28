#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Step 1 of 2 in the locally-run fuzz_script pipeline: builds the
oracle, tapyrus-verify (src/tapyrus-verify.cpp), with
-DBUILD_SCRIPT_VERIFY=ON -DBUILD_FUZZ_TEST=ON and ASan/UBSan -- the same
configuration daily-test.yml's fuzz-script-sweep job builds, in the same
build_fuzz_verify/ directory name (already gitignored via build_*), so
a candidate that validates cleanly here is tested the same way the
nightly sweep will test it once committed.

Generation itself isn't a script: candidate Script programs are written
by Claude Code, interactively, in this checkout -- see README.md next to
this file. fuzz_script_step2_validate.py then runs this binary against
them.

Incremental: re-run after pulling or editing source; CMake only
rebuilds what changed.

Usage:
  fuzz_script_step1_build_verify.py [--build-dir DIR]
"""
import argparse
import asyncio
import os
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent.parent
DEFAULT_BUILD_DIR = REPO_ROOT / "build_fuzz_verify"

sys.path.insert(0, str(SCRIPT_DIR.parent))
from _async_proc import Command  # noqa: E402


class VerifyBinaryBuilder:
    def __init__(self, build_dir: Path):
        self.build_dir = build_dir

    @property
    def binary_path(self) -> Path:
        return self.build_dir / "bin" / "tapyrus-verify"

    async def run(self) -> int:
        await Command(
            "cmake", "-S", REPO_ROOT, "-B", self.build_dir,
            "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
            "-DSANITIZERS=address,undefined",
            "-DBUILD_DAEMON=OFF", "-DBUILD_GUI=OFF", "-DBUILD_CLI=OFF", "-DBUILD_GENESIS=OFF",
            "-DENABLE_WALLET=OFF", "-DENABLE_TESTS=OFF", "-DENABLE_BENCH=OFF", "-DENABLE_ZMQ=OFF",
            "-DBUILD_SCRIPT_VERIFY=ON", "-DBUILD_FUZZ_TEST=ON",
        ).run()
        await Command(
            "cmake", "--build", self.build_dir, "--target", "tapyrus-verify",
            "-j", str(os.cpu_count() or 1),
        ).run()

        print()
        print(f"Built {self.binary_path}")
        print("Next: ask Claude Code to write candidates (see README.md), then run")
        print(f"  {SCRIPT_DIR / 'fuzz_script_step2_validate.py'}")
        return 0


async def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR,
                         help=f"CMake build directory (default: {DEFAULT_BUILD_DIR}).")
    args = parser.parse_args()

    return await VerifyBinaryBuilder(args.build_dir.resolve()).run()


if __name__ == "__main__":
    raise SystemExit(asyncio.run(_main()))
