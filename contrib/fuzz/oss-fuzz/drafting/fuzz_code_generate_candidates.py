#!/usr/bin/env python3
"""Turns each candidate in fuzz_gaps.json (../../fuzz-introspector/) into a
YAML file describing one function needing a fuzz_test_file -- one YAML
per candidate, saved into src/test/fuzz/fuzz_candidates/, meant to be
handed to Claude Code as the spec when drafting that harness by hand.

The YAML schema is OSS-Fuzz-Gen's own "benchmark" format (`-y
<benchmark.yaml>` / `--benchmarks-directory`, schema matches
`benchmark-sets/all/tinyxml2.yaml` upstream) -- kept as-is even though
this repo no longer runs OSS-Fuzz-Gen itself, since it's still a clean,
complete description of one candidate function. "name" is the function's
mangled symbol; falls back to the plain function name when
fuzz_code_find_fuzz_gaps.py didn't capture a mangled one.
target_name/target_path are per-candidate, derived from the function
name: the not-yet-created fuzz_test_file this candidate is waiting on.

Overloads: fuzz_code_find_fuzz_gaps.py emits one candidate entry per real
overload, sharing a function_name but with distinct
return_type/signature/params/line. Disambiguated here by appending the
source line number (__L<line>) to safe_name whenever a function_name
repeats in this run's batch.

Each written YAML carries two dates: yaml_generated_at (when this
candidate first entered the pool) and fuzz_code_generated_at (null until
a harness has actually been drafted for it -- set by hand, since
drafting is a manual Claude Code session, not an automated step this
script tracks). This script fully rewrites every YAML in the pool on
each run; both dates are preserved by reading them back off whatever's
already at out_path before overwriting it. A brand-new candidate gets
yaml_generated_at=now, fuzz_code_generated_at=null. Not tracked in
fuzz_gaps.json itself: that file's entries don't have a final safe_name
(overload disambiguation happens here).
"""
import argparse
import collections
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import yaml
except ImportError as exc:
    raise SystemExit("pip install pyyaml") from exc

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[3]  # drafting/oss-fuzz/fuzz/contrib/ up to repo root
DEFAULT_OUT_DIR = REPO_ROOT / "src" / "test" / "fuzz" / "fuzz_candidates"

PROJECT_NAME = "tapyrus-core"


def _existing_dates(out_path: Path) -> tuple:
    """Returns (yaml_generated_at, fuzz_code_generated_at) read back off
    out_path's current content, or (None, None) if it doesn't exist or
    doesn't parse."""
    if not out_path.is_file():
        return None, None
    try:
        data = yaml.safe_load(out_path.read_text(encoding="utf-8"))
    except yaml.YAMLError:
        return None, None
    if not isinstance(data, dict):
        return None, None
    return data.get("yaml_generated_at"), data.get("fuzz_code_generated_at")


def to_candidate_yaml(candidate: dict, safe_name: str, yaml_generated_at: str,
                      fuzz_code_generated_at) -> dict:
    """Builds the OSS-Fuzz-Gen "benchmark" YAML content for one candidate
    function."""
    name = candidate.get("mangled_name") or candidate["function_name"]
    target_name = f"fuzz_gen_{safe_name}"
    target_path = f"/src/tapyrus-core/src/test/fuzz/fuzz_code/{target_name}.cpp"
    return {
        "project": PROJECT_NAME,
        "language": "c++",
        "target_name": target_name,
        "target_path": target_path,
        "yaml_generated_at": yaml_generated_at,
        "fuzz_code_generated_at": fuzz_code_generated_at,
        "functions": [{
            "name": name,
            "signature": candidate.get("signature", candidate["function_name"]),
            "return_type": candidate.get("return_type", "void"),
            "params": candidate.get("params", []),
        }],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fuzz_gaps_json", type=Path)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--limit", type=int, default=5,
                         help="only generate candidate YAMLs for the top N candidates by complexity")
    args = parser.parse_args()

    gaps_data = json.loads(args.fuzz_gaps_json.read_text(encoding="utf-8"))
    if "candidates" not in gaps_data or "generated_at" not in gaps_data:
        sys.exit(f"error: {args.fuzz_gaps_json} is missing 'candidates' or 'generated_at' -- "
                 "not a fuzz_code_find_fuzz_gaps.py output this version understands.")
    all_candidates = gaps_data["candidates"]
    candidates = all_candidates[:args.limit]
    print(f"{args.fuzz_gaps_json} generated_at: {gaps_data['generated_at']}")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    # Overloads share a function_name; disambiguate the filename by
    # source line whenever a function_name repeats. Counted over the
    # full gaps list, not just this run's --limit-sliced batch: an
    # overload just outside --limit in one run but included in a later
    # run (with a larger --limit) must not flip an already-written
    # bare Foo.yaml to Foo__L<line>.yaml or vice versa.
    name_counts = collections.Counter(c["function_name"] for c in all_candidates)

    for candidate in candidates:
        base_name = candidate["function_name"]
        safe_name = "".join(c if c.isalnum() else "_" for c in base_name)
        if name_counts[base_name] > 1 and candidate.get("line") is not None:
            safe_name = f"{safe_name}__L{candidate['line']}"
        out_path = args.out_dir / f"{safe_name}.yaml"
        yaml_generated_at, fuzz_code_generated_at = _existing_dates(out_path)
        if yaml_generated_at is None:
            yaml_generated_at = datetime.now(timezone.utc).isoformat()
        out_path.write_text(yaml.safe_dump(
            to_candidate_yaml(candidate, safe_name, yaml_generated_at, fuzz_code_generated_at),
            sort_keys=False,
        ), encoding="utf-8")
        print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
