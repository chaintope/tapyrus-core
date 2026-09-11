#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Runs Fuzz Introspector against tapyrus-core via google/oss-fuzz's own
helper.py (see README.md in this directory for why it has to go through
that, rather than a direct standalone invocation).

Usage: fuzz_code_run_introspector.py <path-to-tapyrus-core-checkout> [seconds]

Importable too: fuzz_code_generate_and_draft.py calls IntrospectorRunner
directly rather than shelling out to this file and scraping its stdout.
"""
import argparse
import asyncio
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from _async_proc import Command  # noqa: E402

PROJECT_FILES_DIR = Path(__file__).resolve().parent.parent / "oss-fuzz" / "project"


class IntrospectorRunner:
    """Clones google/oss-fuzz, registers tapyrus-core as a project inside
    it, and runs Fuzz Introspector's own multi-stage instrumented build
    against a real tapyrus-core checkout. Leaves its work directory
    (containing the report) behind on exit -- unlike the other scripts in
    this directory, the caller still needs it after this returns."""

    def __init__(self, tapyrus_core_src: Path, seconds: int = 30):
        self.tapyrus_core_src = tapyrus_core_src
        self.seconds = seconds

    async def run(self) -> Path:
        """Runs the introspector pass, returns the report directory."""
        work_dir = Path(tempfile.mkdtemp())
        oss_fuzz_dir = work_dir / "oss-fuzz"
        await Command(
            "git", "clone", "--depth=1",
            "https://github.com/google/oss-fuzz.git", oss_fuzz_dir,
        ).run()

        project_dir = oss_fuzz_dir / "projects" / "tapyrus-core"
        project_dir.mkdir(parents=True, exist_ok=True)
        for src_file in PROJECT_FILES_DIR.iterdir():
            if src_file.is_file():
                shutil.copy(src_file, project_dir / src_file.name)

        await Command(
            "python3", "infra/helper.py", "introspector", "tapyrus-core",
            "--seconds", str(self.seconds), str(self.tapyrus_core_src),
            cwd=oss_fuzz_dir,
        ).run()

        return oss_fuzz_dir / "build" / "out" / "tapyrus-core" / "introspector-report" / "inspector"


async def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tapyrus_core_src", type=Path)
    parser.add_argument("seconds", type=int, nargs="?", default=30)
    args = parser.parse_args()

    report_dir = await IntrospectorRunner(args.tapyrus_core_src, args.seconds).run()
    print(f"Report: {report_dir / 'fuzz_report.html'}")
    print(report_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(_main()))
