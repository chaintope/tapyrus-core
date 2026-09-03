# OSS-Fuzz-Gen + Claude wiring (draft)

Writes new fuzz_test_file candidates for the candidate functions
`../fuzz-introspector/fuzz_code_find_fuzz_gaps.py` identifies, using
[OSS-Fuzz-Gen](https://github.com/google/oss-fuzz-gen) with Claude as the
generation backend.

A **fuzz_test_file** here means the same thing it does everywhere else in
this repo's fuzz CI: a `FUZZ_TARGET`/libFuzzer-style C++ source file (like
`src/test/fuzz/pstt_parse_fuzz.cpp`) that exercises one function. This
directory's job is to have OSS-Fuzz-Gen write new ones automatically, for
functions Fuzz Introspector found with no fuzz coverage at all.

## This whole pipeline is local-only, not a CI job

`fuzz_code_generate_and_draft.py` runs gap analysis, candidate-file
generation, and drafting in one pass, by hand. There is no CI workflow
for any of it -- once a drafted fuzz_test_file is reviewed and merged, it
becomes a permanent entry in `src/test/fuzz/FUZZ_TARGETS.txt` and runs
daily via libFuzzer from then on with zero further AI involvement. Gap
analysis costs nothing, but drafting spends real GCP Vertex AI dollars
per candidate, so both stay occasional/manual rather than recurring --
same reasoning as `../fuzz4all/fuzz_script_generate_pool.py`.

## Invocation

```
./run_all_experiments.py \
    --model=<claude-model-string> \
    -y <candidate.yaml> \
    --work-dir=<output-dir>
```

`fuzz_code_generate_candidates.py` in this directory turns each `fuzz_gaps.json`
candidate (function name + source file) from the introspector step into a
YAML file under `candidate_pool/` -- one file per candidate, each with its
own `target_name`/`target_path` (a distinct not-yet-created fuzz_test_file
OSS-Fuzz-Gen is asked to write, not a fixed placeholder). OSS-Fuzz-Gen's
own CLI calls this YAML format a "benchmark" (`-y <benchmark.yaml>`,
`--benchmarks-directory`) -- that's their name for the file, not ours; we
call the pool and the generation step "candidate" since from our side
each entry is a candidate function waiting on a fuzz_test_file.

**Confirmed by reading oss-fuzz-gen's actual current source
(`llm_toolkit/models.py`), not just its docs -- this blocks running the
writing step for real until it's resolved:**

1. **Claude access goes through Vertex AI only.** There is no
   bare-`ANTHROPIC_API_KEY` code path in oss-fuzz-gen -- Claude queries go
   through `anthropic.AnthropicVertex(region=region, project_id=project_id)`
   unconditionally. This step needs GCP Vertex AI credentials (project id,
   region, service-account or Workload Identity Federation auth), not the
   `ANTHROPIC_API_KEY` secret used elsewhere in this repo's fuzz CI (the
   Fuzz4All adapter in `../fuzz4all/`, which really does use the
   plain Anthropic API).
2. **Model ID currency.** Every Claude class registered upstream is 3.x-era
   (`vertex_ai_claude-3-5-sonnet` / `vertex_ai_claude-3-opus` /
   `vertex_ai_claude-3-haiku`, dated `@`-suffixed Vertex IDs) -- nothing
   current is registered. `fuzz_code_vertex_claude_patch.py` in this directory adds a
   `ClaudeOpus5` class (`name='vertex_ai_claude-opus-5'`,
   `_vertex_ai_model='claude-opus-5'`) -- append-only
   (`cat fuzz_code_vertex_claude_patch.py >> oss-fuzz-gen/llm_toolkit/models.py`),
   safe against upstream churn because oss-fuzz-gen discovers models via
   `Claude.__subclasses__()` walking, not a separate registration list.

`fuzz_code_generate_and_draft.py` needs GCP Vertex AI credentials
available locally (however your gcloud/ADC setup normally authenticates)
before drafting will actually run -- not provisioned yet, so today the
script's gap-analysis and candidate-generation stages work, but drafting
will fail until that's set up.

## Human review, not auto-merge

Written fuzz_test_files land in `<work-dir>/fixed_targets/`, get copied
into this run's `local_drafts_<timestamp>/<candidate-name>/`, and are
never committed or auto-merged. Per this repo's own working agreement,
AI-authored changes go through human review before landing -- OSS-Fuzz-Gen's
own self-correction loop (fixing build errors, checking coverage) validates
that a fuzz_test_file *compiles and runs*, not that it's one a maintainer
actually wants to keep.

`fuzz_code_generate_and_draft.py`'s last step builds
`local_drafts_<timestamp>/review_code.html` from those drafts --
`fuzz_code_build_review_page.py`, a plain static page with no server and
no external resources, one row per drafted candidate with its full
source inline, a checkbox, and an editable target name. Open it in a
browser, check the ones worth keeping, edit target names if you want
different ones, then use the browser's own File > Save Page As
(Webpage, HTML Only) to save your edits -- the page's own JS mirrors
every checkbox/input change into the saved HTML's attributes so the
saved file alone captures your review.

That saved file is `fuzz_code_land_approved.py`'s only input:

```
./fuzz_code_land_approved.py <path-to-the-saved-review_code.html>
```

It writes each approved candidate's `.cpp` under `src/test/fuzz/`, adds
its `add_executable(...)` block to `src/test/CMakeLists.txt`, and adds
its name to `src/test/fuzz/FUZZ_TARGETS.txt` -- the same three edits this
directory's own docs used to describe as a manual step, now mechanical.
It runs no git commands at all; staging and committing the result stays
a manual step you do yourself afterward.
