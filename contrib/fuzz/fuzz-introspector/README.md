# Fuzz Introspector integration (draft)

Runs Google's [Fuzz Introspector](https://github.com/ossf/fuzz-introspector)
against the `../oss-fuzz/project/` build recipe to answer "which
tapyrus-core functions have no fuzz coverage at all" -- the input the
OSS-Fuzz-Gen + Claude step (`../oss-fuzz/drafting/`) uses to decide what to draft
next as the codebase evolves, rather than a fixed target list going stale.

## How it actually runs

Fuzz Introspector isn't a standalone CLI against an arbitrary source tree --
it runs through `google/oss-fuzz`'s own `infra/helper.py introspector`
command, which needs our project registered as `projects/tapyrus-core/`
inside a checkout of `google/oss-fuzz` itself (the same three files as
`../oss-fuzz/project/`). `fuzz_code_run_introspector.py` does that wiring:

```
python3 infra/helper.py introspector tapyrus-core --seconds 30 <path-to-checked-out-tapyrus-core>
```

This is a genuinely heavy, multi-stage operation (per `oss-fuzz/infra/helper.py`'s
own `introspector` subcommand): builds every fuzz target with ASan, runs
them briefly, rebuilds with coverage instrumentation, extracts coverage,
then rebuilds again with the introspector LLVM plugin -- four separate
compiles of the whole dependency tree. Report lands at
`build/out/tapyrus-core/introspector-report/inspector/` (an HTML report,
`fuzz_report.html`, plus per-run JSON/data files).

**Not executed end-to-end in this session** -- the OSS-Fuzz project files it
depends on (`../oss-fuzz/project/`) are drafts, and a full four-stage
instrumented build is squarely `fuzz_code_generate_and_draft.py`'s own
responsibility to run locally, by hand, when a maintainer wants an
updated gap list, not something to prove out interactively here. There
is no CI job for this step at all -- see `../oss-fuzz/drafting/README.md`'s
note on why gap analysis, candidate generation, and drafting all stayed
out of GitHub Actions. `fuzz_code_find_fuzz_gaps.py` below is written
defensively against the exact JSON schema for that reason (see its own
docstring) -- verify/adjust its parsing against whatever Fuzz Introspector
version is actually installed locally, the first time it runs for real.

## `fuzz_code_find_fuzz_gaps.py`

Reads whatever `inspector/` produces, finds functions Fuzz Introspector
marks as unreached by any current fuzz target, restricts to tapyrus-core's
own untrusted-input-facing directories (`src/rpc/`, `src/script/`,
`src/primitives/`, `src/net_processing.cpp`-style P2P message handlers --
see `UNTRUSTED_INPUT_DIRS` in the script), and writes a ranked candidate
list to `fuzz_gaps.json` for the OSS-Fuzz-Gen step to consume.
