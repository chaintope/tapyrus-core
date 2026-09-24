#!/usr/bin/env python3
"""Turns a Fuzz Introspector report into a ranked list of candidate
functions with no fuzz coverage, for the OSS-Fuzz-Gen + Claude step to
write fuzz_test_files for next.

Reads report_dir/result.json: Fuzz Introspector's per-function report, a
JSON list of records keyed by function_name/function_filename, with
is_reached/reached_by_fuzzers/hitcount for reachability and
cyclomatic_complexity for ranking. Deduplicated by (function_name,
source_file).

Also reads one report_dir/fuzzerLogFile-*.data.yaml sibling (any one --
each independently carries the whole project's function list) for
return_type/signature/params: result.json collapses same-name-same-file
overloads down to a single record, so real signature data comes from
here instead. Overloads are kept distinct by source line number; each
real overload found becomes its own candidate entry (see
fuzz_code_generate_candidates.py for how those get disambiguated into
distinct output filenames). A function absent from the fuzzerLogFile
data is written with no return_type/signature/params at all --
fuzz_code_generate_candidates.py's own return_type="void"/params=[]
fallback covers that.

Writes two JSON files, each {"generated_at": ..., "candidates": [...]}:
--out for functions no harness reaches at all (restricted to
UNTRUSTED_INPUT_DIRS), --undercovered-out for functions an existing
harness statically reaches but never exercised at runtime (0% measured
coverage) -- not restricted to UNTRUSTED_INPUT_DIRS, and excluded from
fuzz_code_generate_candidates.py's input, since deepening an existing
harness is a different task from drafting a new one.
"""
import argparse
import collections
import glob
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import yaml
except ImportError as exc:
    raise SystemExit("pip install pyyaml") from exc

try:
    from yaml import CSafeLoader as _YamlLoader
except ImportError:
    from yaml import SafeLoader as _YamlLoader

# Directories where a function argument plausibly originates from
# untrusted input (RPC/P2P/serialized-data callers).
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


def is_reached_but_undercovered(function_record: dict) -> bool:
    """True if an existing harness statically reaches this function but
    measured runtime coverage is 0%. Requires both fields present."""
    if is_unreached(function_record):
        return False
    coverage = function_record.get("runtime_coverage_percent")
    return coverage is not None and coverage == 0


def in_untrusted_input_dir(source_file: str) -> bool:
    return any(marker in source_file for marker in UNTRUSTED_INPUT_DIRS)


def find_function_records(report_dir: Path):
    result_path = report_dir / "result.json"
    if not result_path.is_file():
        sys.exit(f"error: {result_path} not found -- Fuzz Introspector's per-function analysis "
                 "output (result.json) is missing from this report directory. Confirm "
                 "fuzz_code_step3_analyze_incontainer.py's analyse_end_to_end() call actually "
                 "completed (check its own log for a 'Found data issues' warning).")
    try:
        data = json.loads(result_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        sys.exit(f"error: {result_path} is not valid JSON ({exc})")
    if not isinstance(data, list):
        sys.exit(f"error: {result_path} is not a JSON list at its top level -- Fuzz Introspector's "
                 "result.json schema has apparently changed; update this function to match.")
    yield from data


def load_overloads_by_key(report_dir: Path) -> dict:
    """Returns {(function_name, source_file): [overload, ...]} from the
    first report_dir/fuzzerLogFile-*.data.yaml found (sorted order), each
    overload a dict with return_type/signature/params/line."""
    candidates_paths = sorted(glob.glob(str(report_dir / "fuzzerLogFile-*.data.yaml")))
    if not candidates_paths:
        print(f"warning: no fuzzerLogFile-*.data.yaml found under {report_dir} -- "
              "candidates will be written with no return_type/signature/params "
              "enrichment (fuzz_code_generate_candidates.py's own void/[] fallback "
              "will apply to all of them).", file=sys.stderr)
        return {}

    with open(candidates_paths[0], encoding="utf-8") as f:
        data = yaml.load(f, Loader=_YamlLoader)
    elements = data.get("All functions", {}).get("Elements", [])

    # (name, file, line) isn't always unique on its own, but every
    # duplicate carries an identical signature, so the first is kept.
    seen_lines = set()
    overloads_by_key = collections.defaultdict(list)
    for elem in elements:
        name = elem.get("functionName")
        source_file = elem.get("functionSourceFile")
        line = elem.get("functionLinenumber")
        if not name or not source_file:
            continue
        dedup_key = (name, source_file, line)
        if dedup_key in seen_lines:
            continue
        seen_lines.add(dedup_key)
        arg_names = elem.get("argNames") or []
        arg_types = elem.get("argTypes") or []
        overloads_by_key[(name, source_file)].append({
            "line": line,
            "return_type": elem.get("returnType"),
            "signature": elem.get("signature"),
            "params": [{"name": n.strip(), "type": t} for n, t in zip(arg_names, arg_types)],
        })

    for key, overloads in overloads_by_key.items():
        overloads.sort(key=lambda o: o["line"] or 0)
    return overloads_by_key


def expand_with_overloads(entry: dict, overloads_by_key: dict) -> list:
    """Expands one gap-detection entry into one candidate per overload
    found for its (function_name, source_file). Returns [entry] unchanged
    if no overload data was found."""
    overloads = overloads_by_key.get((entry["function_name"], entry["source_file"]))
    if not overloads:
        return [entry]
    return [{**entry, **overload} for overload in overloads]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report_dir", type=Path, help="Fuzz Introspector's inspector/ output directory")
    parser.add_argument("--out", type=Path, default=Path("fuzz_gaps.json"))
    parser.add_argument("--undercovered-out", type=Path, default=Path("fuzz_gaps_undercovered.json"),
                         help="Functions an existing harness reaches but with 0%% measured runtime "
                              "coverage.")
    args = parser.parse_args()

    if not args.report_dir.is_dir():
        sys.exit(f"error: {args.report_dir} is not a directory -- run fuzz_code_step3_analyze.py first")

    overloads_by_key = load_overloads_by_key(args.report_dir)

    candidates = []
    undercovered = []
    records_with_function_name = 0
    seen = set()
    for record in find_function_records(args.report_dir):
        if not isinstance(record, dict):
            continue
        function_name = record.get("function_name") or record.get("name")
        if not function_name:
            continue
        source_file = record.get("function_filename") or record.get("source_file") or record.get("file") or ""
        dedup_key = (function_name, source_file)
        if dedup_key in seen:
            continue
        seen.add(dedup_key)
        records_with_function_name += 1
        entry = {
            "function_name": function_name,
            "source_file": source_file,
            "complexity": record.get("cyclomatic_complexity") or record.get("complexity"),
        }
        if is_reached_but_undercovered(record):
            undercovered.extend(expand_with_overloads(entry, overloads_by_key))
        if not in_untrusted_input_dir(source_file):
            continue
        if is_unreached(record):
            candidates.extend(expand_with_overloads(entry, overloads_by_key))

    # Zero usable records means a schema/field-name mismatch, not a
    # fully-covered codebase -- fail loudly instead of writing an empty
    # list.
    if records_with_function_name == 0:
        sys.exit(f"error: parsed 0 function records with a function_name from {args.report_dir} -- "
                 "the report directory has JSON files but none matched the expected shape. This "
                 "usually means Fuzz Introspector's output schema changed; inspect a raw *.json "
                 "file under that directory and update this script's field names to match.")

    # Highest complexity first, as a proxy for "most likely to hide a bug".
    candidates.sort(key=lambda c: c["complexity"] or 0, reverse=True)
    undercovered.sort(key=lambda c: c["complexity"] or 0, reverse=True)
    # generated_at lets a maintainer compare this against each pool
    # yaml's own yaml_generated_at/fuzz_code_generated_at.
    generated_at = datetime.now(timezone.utc).isoformat()
    args.out.write_text(
        json.dumps({"generated_at": generated_at, "candidates": candidates}, indent=2),
        encoding="utf-8",
    )
    args.undercovered_out.write_text(
        json.dumps({"generated_at": generated_at, "candidates": undercovered}, indent=2),
        encoding="utf-8",
    )
    print(f"{records_with_function_name} function record(s) parsed; "
          f"{len(candidates)} unreached, untrusted-input-facing function(s) written to {args.out}; "
          f"{len(undercovered)} reached-but-0%-runtime-coverage function(s) written to "
          f"{args.undercovered_out}")


if __name__ == "__main__":
    main()
