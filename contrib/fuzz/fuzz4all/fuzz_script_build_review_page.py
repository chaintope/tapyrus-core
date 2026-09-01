#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Builds review_scripts.html: a plain, self-contained (no server, no
CDN, no external resources) HTML page listing one generation run's
newly added Tapyrus Script candidates, one row per candidate file, with
a checkbox for "keep this one."

Unlike fuzz_code_build_review_page.py's review_code.html, this review is
a PRUNE, not an ADD: fuzz_script_generate_pool.py already writes every
candidate straight into its final home (generated_pool/tapyrus_script/)
-- there's no separate landing location to wire up. Reviewing here means
deciding which of the files already sitting in that directory, from this
run specifically, are worth keeping; fuzz_script_land_approved.py
deletes the unchecked ones (its only job), leaving the checked ones in
place for you to `git add` and commit yourself.

Scoped to one run via --run-prefix: only candidates whose filename
starts with it are listed, not the whole (possibly many-runs-deep) pool
-- this run's review shouldn't re-litigate candidates a previous run
already had reviewed and committed.
"""
import argparse
import html
from pathlib import Path

PAGE_TEMPLATE = """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>fuzz_script candidates -- review</title>
<style>
  body {{ font-family: -apple-system, Helvetica, Arial, sans-serif; margin: 2rem; color: #1a1a1a; }}
  h1 {{ font-size: 1.3rem; }}
  p.hint {{ color: #555; max-width: 60rem; }}
  table {{ border-collapse: collapse; width: 100%; margin-top: 1rem; }}
  th, td {{ border: 1px solid #ccc; padding: 0.5rem; text-align: left; vertical-align: top; }}
  th {{ background: #f0f0f0; }}
  pre {{ background: #f7f7f7; padding: 0.5rem; overflow-x: auto; margin: 0; white-space: pre-wrap; word-break: break-word; }}
  .path {{ font-family: monospace; font-size: 0.9em; }}
  #keep-count {{ font-weight: bold; }}
</style>
</head>
<body>
<h1>fuzz_script candidates from this run -- review</h1>
<p class="hint">
  Check "keep" for the candidates worth keeping, then use your browser's
  <b>File &gt; Save Page As</b> (choose "Webpage, HTML Only") to save your
  edits. Then prune the rest:
</p>
<pre id="land-cmd">contrib/fuzz/fuzz4all/fuzz_script_land_approved.py &lt;path-to-the-file-you-just-saved&gt;</pre>
<p><span id="keep-count">0</span> of {total} candidates currently kept -- everything else gets deleted on land.</p>
<table>
<thead>
<tr><th>Keep</th><th>Filename</th><th>Script</th></tr>
</thead>
<tbody>
{rows}
</tbody>
</table>
<script>
function syncCheckbox(cb) {{
  if (cb.checked) {{ cb.setAttribute("checked", "checked"); }}
  else {{ cb.removeAttribute("checked"); }}
  updateCount();
}}
function updateCount() {{
  var boxes = document.querySelectorAll("input.keep-box");
  var n = 0;
  for (var i = 0; i < boxes.length; i++) {{ if (boxes[i].checked) {{ n++; }} }}
  document.getElementById("keep-count").textContent = n;
}}
document.addEventListener("DOMContentLoaded", updateCount);
</script>
</body>
</html>
"""

ROW_TEMPLATE = """<tr data-candidate-id="{row_id}">
  <td><input type="checkbox" class="keep-box" id="keep-{row_id}" data-filename="{filename_attr}" onchange="syncCheckbox(this)"></td>
  <td class="path">{filename}</td>
  <td><pre>{source_escaped}</pre></td>
</tr>
"""


def build_rows(pool_dir: Path, run_prefix: str) -> "list[str]":
    rows = []
    candidates = sorted(
        p for p in pool_dir.iterdir() if p.is_file() and p.name.startswith(run_prefix)
    )
    for candidate in candidates:
        source_text = candidate.read_text(errors="replace")
        row_id = html.escape(candidate.name, quote=True)
        rows.append(ROW_TEMPLATE.format(
            row_id=row_id,
            filename_attr=html.escape(candidate.name, quote=True),
            filename=html.escape(candidate.name),
            source_escaped=html.escape(source_text),
        ))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pool_dir", type=Path, help="generated_pool/tapyrus_script/")
    parser.add_argument("--run-prefix", required=True,
                         help="only list candidates whose filename starts with this "
                              "(fuzz_script_generate_pool.py's own RUN_PREFIX for this run)")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    rows = build_rows(args.pool_dir, args.run_prefix)
    args.out.write_text(PAGE_TEMPLATE.format(rows="".join(rows), total=len(rows)))
    print(f"wrote {args.out} ({len(rows)} candidate(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
