#!/usr/bin/env python3
"""Reads a review_code.html saved back by a maintainer after using
fuzz_code_build_review_page.py's page (checked boxes = approved, edited
text inputs = the target name to land under), and mechanically does the
one-file landing step: write the .cpp under src/test/fuzz/fuzz_code/.

src/test/CMakeLists.txt globs every fuzz/fuzz_code/*_fuzz.cpp file into its own
executable and wires the phony fuzz_all target to depend on all of them,
so a target name ending in _fuzz needs nothing further registered
anywhere else to build and run in CI.

Deliberately does no git commands at all -- copies files into the
working tree and stops there; staging/committing stays a manual step the
maintainer does themselves afterward, the same as every other generated/
discovered content in this repo.

Everything this script needs (the reviewed source, the approved flag,
the target name) is embedded in the saved review_code.html itself -- it does
not read back from the original local_drafts_<timestamp>/ directory.
"""
import argparse
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]  # drafting/oss-fuzz/fuzz/contrib/ up to repo root
FUZZ_SRC_DIR = REPO_ROOT / "src" / "test" / "fuzz" / "fuzz_code"


class ReviewPageParser(HTMLParser):
    """Extracts one row per candidate from fuzz_code_build_review_page.py's
    saved output: a <tr data-candidate-id> containing an approve checkbox,
    a target-name text input, and a <pre data-role="source"> block."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.rows: "list[dict]" = []
        self._current_row = None
        self._capturing_source = False

    def handle_starttag(self, tag, attrs):
        attrs_dict = dict(attrs)
        if tag == "tr" and "data-candidate-id" in attrs_dict:
            self._current_row = {"approved": False, "target_name": "", "source_parts": []}
        elif tag == "input" and self._current_row is not None:
            if attrs_dict.get("type") == "checkbox":
                self._current_row["approved"] = "checked" in attrs_dict
            elif attrs_dict.get("type") == "text":
                self._current_row["target_name"] = attrs_dict.get("value", "")
        elif tag == "pre" and attrs_dict.get("data-role") == "source":
            self._capturing_source = True

    def handle_endtag(self, tag):
        if tag == "pre":
            self._capturing_source = False
        elif tag == "tr" and self._current_row is not None:
            self._current_row["source"] = "".join(self._current_row.pop("source_parts"))
            self.rows.append(self._current_row)
            self._current_row = None

    def handle_data(self, data):
        if self._capturing_source and self._current_row is not None:
            self._current_row["source_parts"].append(data)


def sanitize_target_name(raw: str) -> str:
    name = re.sub(r"[^A-Za-z0-9_]", "_", raw.strip())
    if not name or not (name[0].isalpha() or name[0] == "_"):
        name = f"_{name}"
    if not name.startswith("fuzz_"):
        name = f"fuzz_{name}"
    return name


def land_candidate(target_name: str, source: str, force: bool) -> bool:
    # src/test/CMakeLists.txt globs fuzz/fuzz_code/*_fuzz.cpp and derives the target
    # name by stripping that suffix and prepending fuzz_ -- so a target
    # named fuzz_<stem> has to land as <stem>_fuzz.cpp for the two to
    # round-trip back to the same name.
    stem = target_name[len("fuzz_"):]
    cpp_path = FUZZ_SRC_DIR / f"{stem}_fuzz.cpp"
    if cpp_path.exists():
        if cpp_path.read_text() == source:
            print(f"  {cpp_path.relative_to(REPO_ROOT)} already present with identical content, skipping")
            return False
        if not force:
            print(f"  ERROR: {cpp_path.relative_to(REPO_ROOT)} already exists with different "
                  f"content -- skipping (rerun with --force to overwrite)", file=sys.stderr)
            return False
    cpp_path.write_text(source)
    print(f"  wrote {cpp_path.relative_to(REPO_ROOT)} (target {target_name})")

    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("review_html", type=Path, help="the saved (edited) review_code.html")
    parser.add_argument("--force", action="store_true",
                         help="overwrite an existing .cpp with different content instead of skipping it")
    args = parser.parse_args()

    page_parser = ReviewPageParser()
    page_parser.feed(args.review_html.read_text())

    approved = [row for row in page_parser.rows if row["approved"]]
    if not approved:
        print("No approved candidates found in this review page (no checked boxes). Nothing to do.")
        return 0

    print(f"Landing {len(approved)} approved candidate(s):")
    landed = 0
    for row in approved:
        target_name = sanitize_target_name(row["target_name"])
        if land_candidate(target_name, row["source"], args.force):
            landed += 1

    print(f"\n{landed} of {len(approved)} approved candidate(s) landed.")
    print("Not committed -- review the working-tree diff and `git add`/commit yourself.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
