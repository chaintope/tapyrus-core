#!/usr/bin/env python3
"""Turns a Fuzz Introspector report into a ranked list of candidate
functions with no fuzz coverage, for the OSS-Fuzz-Gen + Claude step to
write fuzz_test_files for next.

NOT verified against a real Fuzz Introspector run in this repo yet (see
../README.md) -- Fuzz Introspector's exact per-function JSON schema varies
by version and isn't pinned anywhere authoritative enough to hardcode a
field name here with confidence. Rather than guess a field name and produce
a script that silently finds nothing, this scans every JSON file under the
report directory and matches on *any* key that looks like an unreached-ness
flag (see UNREACHED_KEY_CANDIDATES) -- verify this actually matches the
installed introspector version's real output the first time the weekly job
runs, and narrow it down once confirmed.
"""
import argparse
import json
import sys
from pathlib import Path

# Restrict candidates to directories where a function argument plausibly
# originates from untrusted input (RPC/P2P/serialized-data callers) --
# Fuzz Introspector's own reachability graph doesn't know "untrusted" from
# "internal-only", so this is a coarse, repo-specific filter on top of it.
UNTRUSTED_INPUT_DIRS = (
    "src/rpc/",
    "src/script/",
    "src/primitives/",
    "src/net_processing.cpp",
    "src/policy/",
)

# Candidate JSON keys across introspector versions that flag a function as
# not reached by any existing fuzz target. Checked in order; the first key
# present on a function record wins.
UNREACHED_KEY_CANDIDATES = ("is_reached", "reached_by_fuzzers", "hitcount")


def is_unreached(function_record: dict) -> bool:
    for key in UNREACHED_KEY_CANDIDATES:
        if key not in function_record:
            continue
        value = function_record[key]
        if key == "is_reached":
            return not value
        if key == "reached_by_fuzzers":
            return len(value) == 0
        if key == "hitcount":
            return value == 0
    return False


def in_untrusted_input_dir(source_file: str) -> bool:
    return any(marker in source_file for marker in UNTRUSTED_INPUT_DIRS)


def find_function_records(report_dir: Path):
    for json_path in report_dir.rglob("*.json"):
        try:
            data = json.loads(json_path.read_text())
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        # Function-level records show up as either a top-level list, or
        # nested under a key like "project_functions"/"all_functions" --
        # normalize the shape rather than assuming one.
        if isinstance(data, list):
            yield from data
        elif isinstance(data, dict):
            for value in data.values():
                if isinstance(value, list):
                    yield from value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report_dir", type=Path, help="Fuzz Introspector's inspector/ output directory")
    parser.add_argument("--out", type=Path, default=Path("fuzz_gaps.json"))
    args = parser.parse_args()

    if not args.report_dir.is_dir():
        sys.exit(f"error: {args.report_dir} is not a directory -- run fuzz_code_run_introspector.py first")

    candidates = []
    for record in find_function_records(args.report_dir):
        if not isinstance(record, dict):
            continue
        source_file = record.get("source_file") or record.get("file") or ""
        function_name = record.get("function_name") or record.get("name")
        if not function_name or not in_untrusted_input_dir(source_file):
            continue
        if is_unreached(record):
            candidates.append({
                "function_name": function_name,
                "source_file": source_file,
                "complexity": record.get("cyclomatic_complexity") or record.get("complexity"),
            })

    # Highest complexity first -- a rough proxy for "most likely to hide a
    # real bug", same rationale OSS-Fuzz-Gen itself uses for prioritization.
    candidates.sort(key=lambda c: c["complexity"] or 0, reverse=True)
    args.out.write_text(json.dumps(candidates, indent=2))
    print(f"{len(candidates)} unreached, untrusted-input-facing function(s) written to {args.out}")


if __name__ == "__main__":
    main()
