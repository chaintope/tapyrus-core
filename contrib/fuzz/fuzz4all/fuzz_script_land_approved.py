#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Reads a review_scripts.html saved back by a maintainer after using
fuzz_script_build_review_page.py's page (checked "keep" boxes = worth
keeping), and prunes generated_pool/tapyrus_script/ down to just that
run's approved candidates -- deletes every listed candidate whose box is
unchecked, leaves checked ones untouched.

The inverse of fuzz_code_land_approved.py: that script ADDS files into
their final home (drafts start elsewhere first). This one only ever
DELETES, since fuzz_script_generate_pool.py already wrote every
candidate straight into generated_pool/tapyrus_script/, its final home,
before this review ever happens -- there's nothing to add or wire up.

Deliberately does no git commands at all -- deletes files from the
working tree and stops there; staging/committing what's left stays a
manual step you do yourself afterward, the same as every other
generated/discovered content in this repo.
"""
import argparse
from html.parser import HTMLParser
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent.parent
POOL_DIR = SCRIPT_DIR / "generated_pool" / "tapyrus_script"


class ReviewPageParser(HTMLParser):
    """Extracts one row per candidate from fuzz_script_build_review_page.py's
    saved output: each candidate is one <input class="keep-box"> carrying
    its filename in data-filename and its kept/deleted state in whether
    the checked attribute survived the save."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.rows: "list[dict]" = []

    def handle_starttag(self, tag, attrs):
        if tag != "input":
            return
        attrs_dict = dict(attrs)
        if attrs_dict.get("class") != "keep-box":
            return
        self.rows.append({
            "filename": attrs_dict.get("data-filename", ""),
            "keep": "checked" in attrs_dict,
        })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("review_html", type=Path, help="the saved (edited) review_scripts.html")
    args = parser.parse_args()

    page_parser = ReviewPageParser()
    page_parser.feed(args.review_html.read_text())

    if not page_parser.rows:
        print("No candidates found in this review page. Nothing to do.")
        return 0

    kept = 0
    deleted = 0
    for row in page_parser.rows:
        if not row["filename"]:
            continue
        candidate_path = POOL_DIR / row["filename"]
        if row["keep"]:
            kept += 1
            continue
        if candidate_path.exists():
            candidate_path.unlink()
            deleted += 1
            print(f"  deleted {candidate_path.relative_to(REPO_ROOT)}")

    print(f"\n{kept} candidate(s) kept, {deleted} deleted.")
    print("Not committed -- review the working-tree diff and `git add`/commit yourself.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
