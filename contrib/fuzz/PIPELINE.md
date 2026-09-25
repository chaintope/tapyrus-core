# Fuzz pipeline

Two pipelines grow this repo's fuzz coverage: one finds functions with no
fuzz-target coverage (Fuzz Introspector) and drafts a `fuzz_test_file`
for one locally via Claude Code. The other (fuzz_script) has Claude Code
write candidate Tapyrus Script programs locally, validated against a
local sanitizer build of `tapyrus-verify`. Neither makes a paid API
call: the team decided to skip both OSS-Fuzz-Gen's Vertex AI drafting
and Fuzz4All's Anthropic API generation, so there is no API key, spend
tracking or budget cap anywhere in either pipeline.

Every node in the diagrams below is a real external library or service
(Fuzz Introspector, Claude Code, libFuzzer) -- no tapyrus binary or in-repo library gets its own node.
Local orchestration is drawn as the human's (or CI's) own actions,
labeled with the actual script that runs it.

Mermaid's `sequenceDiagram` grammar has no `click`/hyperlink directive
(that's a flowchart/class/state-diagram feature only -- confirmed by
trying it: `mermaid.parse()` rejects `click` inside a `sequenceDiagram`
block), so a script name inside either diagram below can't be a link. The
[Scripts at a glance](#scripts-at-a-glance) table underneath does the
same job instead: every script name there is a real relative link to
that file in this checkout.

## Pipeline A -- fuzz_code

[`oss-fuzz/drafting/`](oss-fuzz/drafting/) + [`oss-fuzz/project/`](oss-fuzz/project/) + [`fuzz-introspector/`](fuzz-introspector/)

```mermaid
sequenceDiagram
    autonumber
    actor Human
    participant Intro as Fuzz Introspector
    participant CC as Claude Code
    participant CI as daily-test.yml<br/>fuzz-code-only
    participant LF as libFuzzer

    rect rgb(251, 236, 238)
    Note over Human,Intro: MANUAL, infrequent -- fuzz_code_step1_build_image.py + fuzz_code_step2_start_container.py (persistent container, reused across runs -- fails fast if src/secp256k1 isn't checked out on the host)
    Human->>Intro: build local Docker image, start container
    end

    rect rgb(251, 236, 238)
    Note over Human,Intro: MANUAL -- fuzz_code_step3_analyze.py (--analysis-only skips the rebuild when only re-ranking after a source change)
    Human->>Intro: build instrumented binary in container, run static analysis
    Intro-->>Human: per-harness data.yaml (real return_type/signature/params, every overload -- result.json alone collapses these)
    end

    rect rgb(251, 236, 238)
    Note over Human: MANUAL -- fuzz_code_find_fuzz_gaps.py, fuzz_code_generate_candidates.py (local, no external call)
    Human->>Human: rank uncovered functions, write src/test/fuzz/fuzz_candidates/*.yaml
    end

    rect rgb(251, 236, 238)
    Note over Human,CC: MANUAL, local -- no paid API call
    Human->>CC: draft the fuzz_test_file for one candidate, using its pool YAML as spec
    CC-->>Human: harness source, compiled + smoke-tested locally
    Human->>Human: writes it under src/test/fuzz/fuzz_code/, git add + commit (manual)
    end

    rect rgb(234, 245, 239)
    Note over CI,LF: DAILY -- daily-test.yml's fuzz-code-only job, no AI, $0
    loop every day
        CI->>CI: fuzz_code_select_slice.py rotates a seed slice into the corpus
        CI->>LF: run the landed harness (libFuzzer + ASan + UBSan) against that corpus for 600s
        LF-->>CI: coverage, crashes, new_seed_candidates
    end
    CI-->>Human: crash artifacts + new_seed_candidates, if any
    end
    Human->>Human: reviews and commits genuinely new seeds (manual)
```

Nothing in this pipeline costs money: gap analysis is local Docker/static
analysis, and drafting is Claude Code writing the harness directly,
compiled and smoke-tested before it's committed. Once a harness is
landed, `fuzz-code-only` runs it daily and free forever -- libFuzzer's
own coverage-guided mutation supplies different inputs every run, not a
further drafting session.

## Pipeline B -- fuzz_script

[`fuzz_script/`](fuzz_script/)

```mermaid
sequenceDiagram
    autonumber
    actor Human
    participant CC as Claude Code
    participant CI as daily-test.yml<br/>fuzz-script-sweep

    rect rgb(251, 236, 238)
    Note over Human: MANUAL -- fuzz_script_step1_build_verify.py (incremental on re-runs)
    Human->>Human: build tapyrus-verify with ASan/UBSan into build_fuzz_verify/ (same configuration as the nightly sweep)
    end

    rect rgb(251, 236, 238)
    Note over Human,CC: MANUAL, local -- no paid API call
    Human->>CC: write a batch of candidate Script programs, using doc/tapyrus/script.md and the existing pool
    CC-->>Human: candidates written straight into src/test/fuzz/fuzz_scripts/ (YYYYMMDD_slug.txt)
    end

    rect rgb(251, 236, 238)
    Note over Human: MANUAL -- fuzz_script_step2_validate.py, then review (no external calls)
    Human->>Human: run tapyrus-verify --fuzz on this batch -- assembler rejects deleted, crashes/failures/timeouts reported
    Human->>Human: review the remaining new files via git status/diff, git add + commit (manual)
    end

    rect rgb(234, 245, 239)
    Note over CI: DAILY -- daily-test.yml's fuzz-script-sweep job, no AI, $0
    loop every day, time-bounded window
        CI->>CI: replay a rotating slice of src/test/fuzz/fuzz_scripts/ against a local build
    end
    CI-->>Human: pass/fail per candidate -- a crash is a real bug
    end
```

No mutation engine here -- unlike pipeline A, every input this pipeline
ever tests was written by Claude Code, which is exactly why the daily
sweep replays a rotating window of an already-committed pool instead of
generating anything itself. Like pipeline A, nothing here costs money
beyond the Claude Code session doing the generation.

## Scripts at a glance

| Script | Pipeline | Cadence | What it does |
| --- | --- | --- | --- |
| [`fuzz_code_step1_build_image.py`](fuzz-introspector/fuzz_code_step1_build_image.py) | fuzz_code | manual, infrequent | Builds the reusable local Docker image (pinned base-image digest) |
| [`fuzz_code_step2_start_container.py`](fuzz-introspector/fuzz_code_step2_start_container.py) | fuzz_code | manual, infrequent | Starts/stops a persistent container from that image, bind-mounting the checkout; fails fast if src/secp256k1 isn't initialized on the host |
| [`fuzz_code_step3_analyze.py`](fuzz-introspector/fuzz_code_step3_analyze.py) | fuzz_code | manual, frequent | Runs the coverage build + Fuzz Introspector analysis inside the running container, then `fuzz_code_find_fuzz_gaps.py` -- `--analysis-only` skips the rebuild when only re-ranking after a source change |
| [`fuzz_code_find_fuzz_gaps.py`](fuzz-introspector/fuzz_code_find_fuzz_gaps.py) | fuzz_code | manual | Parses the per-harness data.yaml into a ranked candidate list, with real return_type/signature/params and every overload included |
| [`fuzz_code_generate_candidates.py`](oss-fuzz/drafting/fuzz_code_generate_candidates.py) | fuzz_code | manual | Turns each candidate into a spec YAML under `src/test/fuzz/fuzz_candidates/` for Claude Code to draft a harness from |
| [`fuzz_code_select_slice.py`](../../src/test/fuzz/fuzz_seed_pool/fuzz_code_select_slice.py) | fuzz_code | daily | Rotates a seed slice into each libFuzzer target's corpus every run |
| [`fuzz_script_step1_build_verify.py`](fuzz_script/fuzz_script_step1_build_verify.py) | fuzz_script | manual | Builds `tapyrus-verify` with ASan/UBSan into `build_fuzz_verify/`, the same configuration the nightly sweep uses |
| [`fuzz_script_step2_validate.py`](fuzz_script/fuzz_script_step2_validate.py) | fuzz_script | manual | Runs `tapyrus-verify --fuzz` on one batch of new candidates; deletes assembler rejects, reports crashes/failures/timeouts -- no git commands |
| [`daily-test.yml`](../../.github/workflows/daily-test.yml): `fuzz-code-only` | fuzz_code | daily | Runs libFuzzer (+ASan/UBSan) against every landed harness -- no AI |
| [`daily-test.yml`](../../.github/workflows/daily-test.yml): `fuzz-script-sweep` | fuzz_script | daily | Replays a rotating window of the committed Script pool -- no AI |

**Both pipelines generate locally, via Claude Code.** OSS-Fuzz-Gen's
Vertex AI drafting path and fuzz_script's Fuzz4All + Anthropic API
generator are both gone: every `fuzz_test_file` and every Script
candidate is written by asking Claude Code directly, then compiled or
validated locally before it's committed, at no cost beyond the Claude
Code session doing the work. Fuzz4All couldn't simply be pointed at
Claude Code instead -- its generation loop needs a model it can call
programmatically once per candidate -- see
[`fuzz_script/README.md`](fuzz_script/README.md#why-fuzz4all-was-removed).

**Python + asyncio, not bash.** `fuzz_code_step1_build_image.py`/
`fuzz_code_step2_start_container.py`/`fuzz_code_step3_analyze.py` and
`fuzz_script_step1_build_verify.py` are Python, not shell -- letting
`step2`'s persistent-container management and `step3`'s in-container
analysis share real code (subprocess wrappers, error handling) instead
of duplicating it across `.sh` scripts.

**No review pages.** Both pipelines generate inside an interactive
Claude Code session, so their output is reviewed there and as ordinary
uncommitted files (`git status`/`git diff`) -- fuzz_code one harness per
request under `src/test/fuzz/fuzz_code/`, fuzz_script one batch under
`src/test/fuzz/fuzz_scripts/`, already filtered by
`fuzz_script_step2_validate.py`. Nothing needs a separate HTML review
page or landing script.
