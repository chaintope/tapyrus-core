#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The one place that knows the src/test/fuzz/fuzz_scripts/ pool format.

The pool is a set of batch files, `<YYYYMMDD>_<batch_slug>.txt`, each
holding many candidate Script programs:

  - one program per line, in ParseScript mnemonic form;
  - lines starting with `#` are comments -- by convention the comment
    directly above a program (no blank line between) names it;
  - blank lines are ignored (use them to separate sections);
  - a line consisting of exactly `<empty>` is the empty script.

A candidate is identified as `<batch file>:<line number>`.
tapyrus-verify --fuzz takes one program per file (ParseScript treats
newlines as ordinary whitespace, so a whole batch file would assemble
into one long script), so every consumer runs candidates from temporary
single-program files written here.

Used as a module by fuzz_script_step2_validate.py, and as a CLI by
daily-test.yml's fuzz-script-sweep job:

  fuzz_script_pool.py list [--prefix P]            print every candidate id
  fuzz_script_pool.py export OUT_DIR [--prefix P]  write each candidate to
                                                   OUT_DIR/<batch>_L<line>.txt
  fuzz_script_pool.py check                        fail on a malformed pool
                                                   (run by test/lint/)
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterable, List

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent.parent
DEFAULT_POOL_DIR = REPO_ROOT / "src" / "test" / "fuzz" / "fuzz_scripts"


class Candidate:
    EMPTY_MARKER = "<empty>"

    def __init__(self, path: Path, line_no: int, text: str):
        self.path = path
        self.line_no = line_no
        self.text = text

    @property
    def id(self) -> str:
        return f"{self.path.name}:{self.line_no}"

    @property
    def program(self) -> str:
        """The text tapyrus-verify --fuzz should assemble."""
        return "" if self.text == self.EMPTY_MARKER else self.text

    @property
    def export_name(self) -> str:
        # Zero-padded so a plain sort lists candidates in line order.
        return f"{self.path.stem}_L{self.line_no:05d}.txt"

    def write_to(self, directory: Path) -> Path:
        out = directory / self.export_name
        out.write_text(self.program + "\n" if self.program else "", encoding="utf-8")
        return out


class BatchFile:
    def __init__(self, path: Path):
        self.path = path

    def _lines(self) -> List[str]:
        return self.path.read_text(encoding="utf-8").splitlines()

    @staticmethod
    def _is_program(line: str) -> bool:
        stripped = line.strip()
        return bool(stripped) and not stripped.startswith("#")

    def candidates(self) -> List[Candidate]:
        return [Candidate(self.path, number, line.strip())
                for number, line in enumerate(self._lines(), start=1)
                if self._is_program(line)]

    def remove(self, candidates: Iterable[Candidate]) -> None:
        """Deletes the given candidates' lines, plus the comment block
        directly above each (its name), leaving section comments that are
        separated by a blank line untouched."""
        doomed = {c.line_no for c in candidates if c.path == self.path}
        if not doomed:
            return
        lines = self._lines()
        drop = set()
        for line_no in doomed:
            drop.add(line_no)
            above = line_no - 1
            while above >= 1 and lines[above - 1].strip().startswith("#"):
                drop.add(above)
                above -= 1
        kept = [line for number, line in enumerate(lines, start=1) if number not in drop]
        self.path.write_text("\n".join(kept) + "\n" if kept else "", encoding="utf-8")


class ScriptPool:
    def __init__(self, pool_dir: Path = DEFAULT_POOL_DIR):
        self.pool_dir = pool_dir

    def batch_files(self, prefix: str = "") -> List[BatchFile]:
        return [BatchFile(p) for p in sorted(self.pool_dir.glob("*.txt")) if p.name.startswith(prefix)]

    def candidates(self, prefix: str = "") -> List[Candidate]:
        return [c for batch in self.batch_files(prefix) for c in batch.candidates()]


class PoolChecker:
    """Format checks cheap enough for the lint job, so a malformed batch
    fails its PR instead of the nightly sweep. Does not assemble programs:
    that needs a tapyrus-verify build (fuzz_script_step2_validate.py)."""
    BATCH_NAME = re.compile(r"^[0-9]{8}_[a-z0-9_]+\.txt$")
    ALLOWED_OTHER_FILES = {"README.md"}

    def __init__(self, pool: ScriptPool):
        self.pool = pool
        self.errors: List[str] = []

    def run(self) -> List[str]:
        self.errors = []
        self._check_file_names()
        total = 0
        for batch in self.pool.batch_files():
            try:
                count = len(batch.candidates())
            except UnicodeDecodeError as e:
                self.errors.append(f"{batch.path.name}: not valid UTF-8 ({e})")
                continue
            if count == 0:
                self.errors.append(f"{batch.path.name}: holds no programs")
            total += count
        if total == 0 and not self.errors:
            self.errors.append(f"{self.pool.pool_dir}: pool holds no candidates")
        elif not self.errors:
            self._check_export(total)
        return self.errors

    def _check_file_names(self) -> None:
        for path in sorted(self.pool.pool_dir.iterdir()):
            if path.name in self.ALLOWED_OTHER_FILES:
                continue
            if not path.is_file() or not self.BATCH_NAME.match(path.name):
                self.errors.append(f"{path.name}: not a batch file; expected <YYYYMMDD>_<batch_slug>.txt")

    def _check_export(self, total: int) -> None:
        # The sweep job runs candidates from exported files, so check that
        # path too: one file per candidate, holding exactly its program.
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp)
            for candidate in self.pool.candidates():
                written = candidate.write_to(out_dir).read_text(encoding="utf-8")
                if written.strip() != candidate.program:
                    self.errors.append(f"{candidate.id}: exported file does not hold its program")
            exported = len(list(out_dir.iterdir()))
            if exported != total:
                self.errors.append(f"export wrote {exported} file(s) for {total} candidate(s)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pool-dir", type=Path, default=DEFAULT_POOL_DIR)
    sub = parser.add_subparsers(dest="command", required=True)
    list_cmd = sub.add_parser("list", help="print every candidate id")
    list_cmd.add_argument("--prefix", default="")
    export_cmd = sub.add_parser("export", help="write each candidate to its own file")
    export_cmd.add_argument("out_dir", type=Path)
    export_cmd.add_argument("--prefix", default="")
    sub.add_parser("check", help="fail on a malformed pool")
    args = parser.parse_args()

    if args.command == "check":
        errors = PoolChecker(ScriptPool(args.pool_dir)).run()
        for error in errors:
            print(error, file=sys.stderr)
        return 1 if errors else 0

    candidates = ScriptPool(args.pool_dir).candidates(args.prefix)
    if args.command == "list":
        for candidate in candidates:
            print(candidate.id)
    else:
        args.out_dir.mkdir(parents=True, exist_ok=True)
        for candidate in candidates:
            candidate.write_to(args.out_dir)
        print(f"exported {len(candidates)} candidate(s) to {args.out_dir}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
