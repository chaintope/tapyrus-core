#!/usr/bin/env python3
"""Reads a review_code.html saved back by a maintainer after using
fuzz_code_build_review_page.py's page (checked boxes = approved, edited
text inputs = the target name to land under), and mechanically does the
three-file landing step README.md and
src/test/fuzz/FUZZ_TARGETS.txt both describe as manual: write the .cpp
under src/test/fuzz/, add its add_executable(...) block to
src/test/CMakeLists.txt, and add its name to FUZZ_TARGETS.txt.

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
FUZZ_SRC_DIR = REPO_ROOT / "src" / "test" / "fuzz"
FUZZ_TARGETS_FILE = FUZZ_SRC_DIR / "FUZZ_TARGETS.txt"
CMAKELISTS_FILE = REPO_ROOT / "src" / "test" / "CMakeLists.txt"
CMAKE_INSERT_BEFORE = "endif(BUILD_FUZZ_TEST)"

CMAKE_BLOCK_TEMPLATE = """add_executable({name} EXCLUDE_FROM_ALL
    fuzz/{name}.cpp
)
target_include_directories({name}
    PRIVATE
        ${{Boost_INCLUDE_DIRS}}
        $<BUILD_INTERFACE:${{PROJECT_SOURCE_DIR}}/../univalue/include>
)
target_link_libraries({name}
    PRIVATE
        core_interface
        tapyrus_server
        tapyrus_common
        tapyrus_util
        tapyrus_consensus
        leveldb
        univalue
        secp256k1
        tapyrus_crypto
        Boost::headers
)

"""


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
    return name


def land_candidate(target_name: str, source: str, force: bool) -> bool:
    cpp_path = FUZZ_SRC_DIR / f"{target_name}.cpp"
    if cpp_path.exists():
        if cpp_path.read_text() == source:
            print(f"  {cpp_path.relative_to(REPO_ROOT)} already present with identical content, skipping")
            return False
        if not force:
            print(f"  ERROR: {cpp_path.relative_to(REPO_ROOT)} already exists with different "
                  f"content -- skipping (rerun with --force to overwrite)", file=sys.stderr)
            return False
    cpp_path.write_text(source)
    print(f"  wrote {cpp_path.relative_to(REPO_ROOT)}")

    targets_text = FUZZ_TARGETS_FILE.read_text()
    if not re.search(rf"^{re.escape(target_name)}$", targets_text, re.MULTILINE):
        if not targets_text.endswith("\n"):
            targets_text += "\n"
        targets_text += f"{target_name}\n"
        FUZZ_TARGETS_FILE.write_text(targets_text)
        print(f"  added {target_name} to {FUZZ_TARGETS_FILE.relative_to(REPO_ROOT)}")

    cmake_text = CMAKELISTS_FILE.read_text()
    if f"add_executable({target_name} " not in cmake_text:
        block = CMAKE_BLOCK_TEMPLATE.format(name=target_name)
        cmake_text = cmake_text.replace(CMAKE_INSERT_BEFORE, block + CMAKE_INSERT_BEFORE, 1)
        CMAKELISTS_FILE.write_text(cmake_text)
        print(f"  added add_executable({target_name} ...) block to {CMAKELISTS_FILE.relative_to(REPO_ROOT)}")

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
