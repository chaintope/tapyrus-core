#!/usr/bin/env python3
"""Turns each candidate in fuzz_gaps.json (../fuzz-introspector/) into a
YAML file describing one function OSS-Fuzz-Gen should write a fuzz_test_file
for -- one YAML per candidate, saved into candidate_pool/.

Why the YAML content still says "benchmark": that's OSS-Fuzz-Gen's own
name for this file, not a name we chose. Its CLI takes `-y <benchmark.yaml>`
/ `--benchmarks-directory`, and its own examples live under
`benchmark-sets/all/*.yaml` upstream (schema here confirmed against
`benchmark-sets/all/tinyxml2.yaml`). We call the things going into
candidate_pool/ "candidates" because that's what they are from our side
(candidate functions waiting to have a fuzz_test_file written for them);
OSS-Fuzz-Gen's tooling just happens to call the YAML schema itself a
"benchmark" internally -- both names refer to the exact same file.

The "name" field OSS-Fuzz-Gen expects is the function's *mangled* symbol
name -- if fuzz_code_find_fuzz_gaps.py's introspector parsing captured one, thread it
through as candidate["mangled_name"]; this script falls back to the plain
function name otherwise, which OSS-Fuzz-Gen may not be able to resolve
against the built binary. Verify against a real run before relying on the
fallback.

target_name/target_path are per-candidate, not a fixed constant: each
candidate YAML asks OSS-Fuzz-Gen to write a *new* fuzz_test_file (one that
does not exist yet -- OSS-Fuzz-Gen creates it), so every candidate needs
its own distinct target_name/target_path derived from its function name.
An earlier version of this script hardcoded both to fuzz_pstt_parse's
existing fuzz_test_file for every candidate, which meant every generated
YAML claimed the same already-written file regardless of which function it
was actually about.
"""
import argparse
import json
from pathlib import Path

try:
    import yaml
except ImportError as exc:
    raise SystemExit("pip install pyyaml") from exc

PROJECT_NAME = "tapyrus-core"


def to_candidate_yaml(candidate: dict, safe_name: str) -> dict:
    """Builds the OSS-Fuzz-Gen "benchmark" YAML content for one candidate
    function -- see the module docstring for why that upstream file format
    is called a benchmark."""
    name = candidate.get("mangled_name") or candidate["function_name"]
    target_name = f"fuzz_gen_{safe_name}"
    target_path = f"/src/tapyrus-core/src/test/fuzz/{target_name}.cpp"
    return {
        "project": PROJECT_NAME,
        "language": "c++",
        "target_name": target_name,
        "target_path": target_path,
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
    parser.add_argument("--out-dir", type=Path, default=Path("candidate_pool"))
    parser.add_argument("--limit", type=int, default=5,
                         help="only generate candidate YAMLs for the top N candidates by complexity")
    args = parser.parse_args()

    candidates = json.loads(args.fuzz_gaps_json.read_text())[:args.limit]
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for candidate in candidates:
        safe_name = "".join(c if c.isalnum() else "_" for c in candidate["function_name"])
        out_path = args.out_dir / f"{safe_name}.yaml"
        out_path.write_text(yaml.safe_dump(to_candidate_yaml(candidate, safe_name), sort_keys=False))
        print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
