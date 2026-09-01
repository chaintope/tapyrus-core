#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Selects a deterministic rotating slice of a fuzz input pool for one CI run.

Used by daily-test.yml's fuzz-code-only job: a large per-target pool of
libFuzzer seed inputs lives under test/fuzz_seed_pool/<target>/ (one file
per input), and this script picks a different `batch_size`-sized slice
every run so consecutive runs anchor their mutation exploration on
different seeds, without any "mark as used" state written back to the
repo. (The Script-corpus pool under contrib/fuzz/fuzz4all/generated_pool/
uses its own time-bounded rotating-window logic instead, inlined in
daily-test.yml's fuzz-script-sweep job rather than this script -- see
that job's own header comment for why.) The slice is a pure function of
(pool contents, batch_size, run_number):

    offset = (run_number * batch_size) % pool_size

so a given run_number always resolves to the same slice (reproducible from
the CI run number alone, visible in the Actions run URL -- useful when
reporting a crash found by a specific run) and consecutive run numbers walk
through the whole pool before wrapping back to the start.

Usage:
    fuzz_code_select_slice.py <pool_dir> <batch_size> <run_number> [--out <dir>]

Prints the selected file paths (one per line) to stdout. With --out, also
copies the selected files into that directory. Exits 0 with an empty
selection and a message on stderr if the pool is empty (caller decides
whether an empty pool means "needs an initial generation batch").

Also emits, on stderr, whether this run completes a full cycle of the pool
(the slice wraps past the end) -- callers that want to trigger regeneration
on exhaustion can grep for "CYCLE_COMPLETE=1".
"""
import argparse
import shutil
import sys
from pathlib import Path


def select_slice(pool_dir: Path, batch_size: int, run_number: int) -> tuple[list[Path], bool]:
    # Every pool directory carries a README.md documenting the convention
    # (see test/fuzz_seed_pool/, contrib/fuzz/fuzz4all/generated_pool/,
    # contrib/fuzz/oss-fuzz/drafting/candidate_pool/) -- exclude it so it never gets
    # rotated in as if it were a real seed/candidate file.
    files = sorted(p for p in pool_dir.iterdir() if p.is_file() and p.name != "README.md")
    pool_size = len(files)
    if pool_size == 0:
        return [], False

    offset = (run_number * batch_size) % pool_size
    end = offset + batch_size
    selected = files[offset:end]
    wrapped = end > pool_size
    if wrapped:
        selected += files[: end - pool_size]

    return selected, wrapped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pool_dir", type=Path)
    parser.add_argument("batch_size", type=int)
    parser.add_argument("run_number", type=int)
    parser.add_argument("--out", type=Path, default=None,
                         help="Copy the selected files into this directory")
    args = parser.parse_args()

    if not args.pool_dir.is_dir():
        print(f"pool directory does not exist: {args.pool_dir}", file=sys.stderr)
        return 1

    selected, wrapped = select_slice(args.pool_dir, args.batch_size, args.run_number)

    if not selected:
        print(f"POOL_EMPTY=1", file=sys.stderr)
        print(f"pool is empty: {args.pool_dir} -- needs an initial generation batch",
              file=sys.stderr)
        return 0

    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        for f in selected:
            shutil.copy2(f, args.out / f.name)

    for f in selected:
        print(f)

    print(f"CYCLE_COMPLETE={'1' if wrapped else '0'}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
