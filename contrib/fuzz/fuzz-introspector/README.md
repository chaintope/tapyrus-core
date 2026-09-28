# Fuzz Introspector integration (draft)

Runs Google's [Fuzz Introspector](https://github.com/ossf/fuzz-introspector)
against the `../oss-fuzz/project/` build recipe to answer "which
tapyrus-core functions have no fuzz coverage at all" -- the input
`../oss-fuzz/drafting/fuzz_code_generate_candidates.py` turns into a
candidate spec for whoever (a human, via Claude Code) drafts the next
harness, rather than a fixed target list going stale.

## How it actually runs

Fuzz Introspector isn't a standalone CLI against an arbitrary source tree --
running it needs our project registered as `projects/tapyrus-core/` inside a
checkout of `google/oss-fuzz` itself (the same three files as
`../oss-fuzz/project/`). Three scripts in this directory drive that, each a
separate, independently re-runnable step:

1. **`fuzz_code_step1_build_image.py`** -- builds `../oss-fuzz/project/Dockerfile`
   into a local, reusable image (pinned base-image digest). Infrequent/manual;
   only needed again when the Dockerfile or pinned digest changes.
2. **`fuzz_code_step2_start_container.py`** -- starts one persistent container
   from that image, bind-mounting a live tapyrus-core checkout (so host edits
   are visible immediately, no rebuild needed) plus a local output and ccache
   directory. Fails fast with a clear message if `src/secp256k1` isn't
   initialized on the host yet. `--stop` tears it down; otherwise it's meant
   to stay running between sessions (`sleep infinity`, near-zero idle cost).
3. **`fuzz_code_step3_analyze.py`** -- execs into that running container to run
   a coverage-sanitizer build plus Fuzz Introspector's own Python API
   (`analyse_end_to_end()` + `correlate_binaries_to_logs()`), then feeds the
   result through `fuzz_code_find_fuzz_gaps.py`. Meant to be run often: re-run
   any time the checkout's source changes, with `--analysis-only` skipping
   even the coverage rebuild once binaries already exist under the
   container's `/out` mount (only `correlate_binaries_to_logs()` reads them;
   the analysis itself is pure source-text parsing with no need for
   freshly-built binaries).

Confirmed working end-to-end multiple times (most recently a ~13-minute run on
2026-09-18, from a cold container to a written `fuzz_gaps.json` with 434 real
candidates) -- earlier attempts hit several real bugs along the way, since
fixed: an oss-fuzz-gen/pip dependency gap, an arm64/amd64 Docker platform
mismatch, an outdated system Boost, a missing `BUILD_FUZZ_TEST` flag, a
sanitizer-mode/flag translation bug, dynamic-vs-static libevent linking, a
CMake `find_library()` suffix-reset gotcha, a shared-header harness pattern
that defeated Fuzz Introspector's non-preprocessing tree-sitter analysis (see
`../../../src/test/fuzz/fuzz_code/pstt_fuzz_driver.h`'s own comment), a wrong
JSON field name in `fuzz_code_find_fuzz_gaps.py`, and a `FrontendAnalyser`
second-pass that ignored the scoped source tree via `$SRC` -- see this
directory's scripts' own comments and git history for the specifics.

Genuinely heavy under the hood: build with ASan, rebuild with coverage
instrumentation, extract coverage, then a tree-sitter-based static source
analysis -- confirmed to take a meaningful chunk of an hour end-to-end even
against this pipeline's own scoped, narrowed source tree (which excludes
vendored third-party libraries and this project's own Qt/bench code -- see
`fuzz_code_step3_analyze_incontainer.py`'s `VENDORED_DIRS`/
`IRRELEVANT_PROJECT_DIRS`).

## `fuzz_code_find_fuzz_gaps.py`

Reads `inspector/result.json` specifically -- Fuzz Introspector's own
per-function analysis output, confirmed directly against a real run (every
record uses `function_filename`; earlier speculative multi-file scanning also
worked but inflated the reported record count by pulling in unrelated nested
lists from other report files). Finds functions marked unreached by any
current fuzz target, restricts to tapyrus-core's own untrusted-input-facing
directories (`src/rpc/`, `src/script/`, `src/primitives/`,
`src/net_processing.cpp`-style P2P message handlers, `src/policy/` -- see
`UNTRUSTED_INPUT_DIRS` in the script), dedupes by `(function_name,
source_file)`, and writes a ranked candidate list to `fuzz_gaps.json` for
`fuzz_code_generate_candidates.py` to consume. Fails loudly (nonzero exit) rather than writing
an empty list if it parses zero function records at all -- almost always a
sign Fuzz Introspector's schema changed, not that the codebase is genuinely
fully covered.
