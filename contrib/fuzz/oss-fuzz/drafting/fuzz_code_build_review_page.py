#!/usr/bin/env python3
"""Builds review_code.html: a plain, self-contained (no server, no CDN, no
external resources) HTML page listing every candidate fuzz_test_file
OSS-Fuzz-Gen drafted this run, one row per candidate .cpp file, with a
checkbox to approve it and an editable target-name field.

Written for a specific offline workflow: fuzz_code_generate_and_draft.py
writes this file into the run's local_drafts_<timestamp>/ directory. A
maintainer opens it directly (file://) in a browser, checks the
candidates worth keeping, optionally edits their suggested target names,
then uses the browser's own "Save Page As -> Webpage, HTML Only" to save
their edits back to an .html file -- the browser serializes the current
checkbox/input state into the saved markup because this page's own JS
mirrors every checkbox and text-input change into the matching HTML
*attribute* (checked=/value=), not just the DOM property Save Page As
would otherwise miss. That saved file is fuzz_code_land_approved.py's
only input: everything it needs (the reviewed C++ source itself, the
approved flag, the target name) is embedded in the page, so the original
local_drafts_<timestamp>/ directory doesn't need to still exist.

No client-side "land" button: doing that for real (writing into this
git checkout) needs local filesystem access a plain static HTML page
opened via file:// cannot get without a server or a per-session browser
permission grant -- out of scope here, see the docstring above for the
save-then-run-a-script workflow used instead.

Schema note: OSS-Fuzz-Gen's exact fixed_targets/ output layout for a
drafting run hasn't been exercised end-to-end in this repo yet (see
README.md's GCP Vertex AI credentials note), so this
script deliberately doesn't assume a fixed filename pattern -- it globs
for any C/C++ source file under each candidate's copied output directory.
Verify/adjust against the real layout the first time a drafting run
completes for real.
"""
import argparse
import html
from pathlib import Path

try:
    import yaml
except ImportError as exc:
    raise SystemExit("pip install pyyaml") from exc

SOURCE_EXTENSIONS = (".cpp", ".cc", ".cxx", ".c")

PAGE_TEMPLATE = """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>fuzz_code drafts -- review</title>
<style>
  body {{ font-family: -apple-system, Helvetica, Arial, sans-serif; margin: 2rem; color: #1a1a1a; }}
  h1 {{ font-size: 1.3rem; }}
  p.hint {{ color: #555; max-width: 60rem; }}
  table {{ border-collapse: collapse; width: 100%; margin-top: 1rem; }}
  th, td {{ border: 1px solid #ccc; padding: 0.5rem; text-align: left; vertical-align: top; }}
  th {{ background: #f0f0f0; }}
  input[type=text] {{ width: 22rem; font-family: monospace; }}
  pre {{ background: #f7f7f7; padding: 0.75rem; overflow-x: auto; max-height: 28rem; margin: 0; }}
  details summary {{ cursor: pointer; color: #06c; }}
  .path {{ font-family: monospace; font-size: 0.85em; color: #555; }}
  #approved-count {{ font-weight: bold; }}
</style>
</head>
<body>
<h1>fuzz_code drafted candidates -- review</h1>
<p class="hint">
  Check the candidates worth keeping, edit their target name if you want a
  different one, then use your browser's <b>File &gt; Save Page As</b>
  (choose "Webpage, HTML Only") to save your edits. Then run:
</p>
<pre id="land-cmd">contrib/fuzz/oss-fuzz/drafting/fuzz_code_land_approved.py &lt;path-to-the-file-you-just-saved&gt;</pre>
<p><span id="approved-count">0</span> of {total} candidates currently approved.</p>
<table>
<thead>
<tr><th>Approve</th><th>Function</th><th>Target name (src/test/fuzz/&lt;name&gt;.cpp)</th><th>Source</th></tr>
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
function syncInput(inp) {{
  inp.setAttribute("value", inp.value);
}}
function updateCount() {{
  var boxes = document.querySelectorAll("input.approve-box");
  var n = 0;
  for (var i = 0; i < boxes.length; i++) {{ if (boxes[i].checked) {{ n++; }} }}
  document.getElementById("approved-count").textContent = n;
}}
document.addEventListener("DOMContentLoaded", updateCount);
</script>
</body>
</html>
"""

ROW_TEMPLATE = """<tr data-candidate-id="{row_id}">
  <td><input type="checkbox" class="approve-box" id="approve-{row_id}" onchange="syncCheckbox(this)"></td>
  <td>{function_name}<br><span class="path">{source_path}</span></td>
  <td><input type="text" value="{target_name}" oninput="syncInput(this)"></td>
  <td><details><summary>view source ({line_count} lines)</summary><pre data-role="source">{source_escaped}</pre></details></td>
</tr>
"""


def function_name_for(pool_dir: Path, candidate_name: str) -> str:
    yaml_path = pool_dir / f"{candidate_name}.yaml"
    if not yaml_path.exists():
        return "(unknown -- no matching candidate_pool/*.yaml found)"
    try:
        data = yaml.safe_load(yaml_path.read_text())
        return data["functions"][0]["name"]
    except (yaml.YAMLError, KeyError, IndexError, TypeError):
        return "(unknown -- candidate_pool/*.yaml did not parse as expected)"


def default_target_name(candidate_name: str, source_file: Path, index: int, count: int) -> str:
    base = f"fuzz_gen_{candidate_name}"
    return base if count == 1 else f"{base}_{index}"


def build_rows(draft_dir: Path, pool_dir: Path) -> "list[str]":
    rows = []
    for candidate_dir in sorted(p for p in draft_dir.iterdir() if p.is_dir()):
        candidate_name = candidate_dir.name
        source_files = sorted(
            p for p in candidate_dir.rglob("*") if p.suffix in SOURCE_EXTENSIONS
        )
        function_name = function_name_for(pool_dir, candidate_name)
        for index, source_file in enumerate(source_files, start=1):
            source_text = source_file.read_text(errors="replace")
            row_id = html.escape(f"{candidate_name}__{index}", quote=True)
            rows.append(ROW_TEMPLATE.format(
                row_id=row_id,
                function_name=html.escape(function_name),
                source_path=html.escape(str(source_file.relative_to(draft_dir))),
                target_name=html.escape(
                    default_target_name(candidate_name, source_file, index, len(source_files)),
                    quote=True,
                ),
                line_count=source_text.count("\n") + 1,
                source_escaped=html.escape(source_text),
            ))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("draft_dir", type=Path, help="this run's local_drafts_<timestamp>/ directory")
    parser.add_argument("--pool-dir", type=Path, required=True,
                         help="candidate_pool/ directory, to look up each candidate's function name")
    parser.add_argument("--out", type=Path, default=None,
                         help="defaults to <draft_dir>/review_code.html")
    args = parser.parse_args()

    out_path = args.out or (args.draft_dir / "review_code.html")
    rows = build_rows(args.draft_dir, args.pool_dir)
    out_path.write_text(PAGE_TEMPLATE.format(rows="".join(rows), total=len(rows)))
    print(f"wrote {out_path} ({len(rows)} candidate(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
