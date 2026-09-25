# Candidate YAML generation

Turns each candidate function `../../fuzz-introspector/fuzz_code_find_fuzz_gaps.py`
identifies (no fuzz coverage at all) into a YAML file under
`src/test/fuzz/fuzz_candidates/` -- the spec a human hands to Claude Code
when asking it to draft a `fuzz_test_file` for that function.

A **fuzz_test_file** here means the same thing it does everywhere else in
this repo's fuzz CI: a `FUZZ_TARGET`/libFuzzer-style C++ source file (like
`src/test/fuzz/fuzz_code/pstt_parse_fuzz.cpp`) that exercises one
function.

## Invocation

```sh
./fuzz_code_generate_candidates.py <fuzz_gaps.json> [--limit N]
```

Reads `fuzz_gaps.json` (written by
`../../fuzz-introspector/fuzz_code_step3_analyze.py`) and writes one YAML
per candidate under `src/test/fuzz/fuzz_candidates/`, each with its own
`target_name`/`target_path` plus the real `return_type`/`signature`/
`params` `fuzz_code_find_fuzz_gaps.py` extracted. Overloads sharing a
function name are disambiguated by appending the source line
(`__L<line>`) to the filename.

The YAML schema is OSS-Fuzz-Gen's own "benchmark" format (`-y
<benchmark.yaml>`, schema matches upstream's own
`benchmark-sets/all/tinyxml2.yaml`) -- kept as-is even though this repo
no longer runs OSS-Fuzz-Gen itself, since it's still a clean, complete
description of one candidate function (name, signature, params, return
type) that's just as useful as a hand-drafting spec.

Each written YAML carries two dates: `yaml_generated_at` (when this
candidate first entered the pool) and `fuzz_code_generated_at` (null
until a harness has actually been drafted for it, see below). This
script fully rewrites every YAML in the pool on each run; both dates are
preserved by reading them back off whatever's already at the output path
before overwriting it.

## Drafting is local, by hand, via Claude Code

There is no automated drafting step and no CI workflow for any of
this -- gap analysis and candidate-YAML generation are the only
automated parts. Turning a candidate YAML into an actual
`fuzz_test_file` means asking Claude Code (interactively, in this
checkout) to write one, using the candidate's own YAML as the spec:
function name, signature, params, return type, and source file. Claude
Code writes the harness directly to
`src/test/fuzz/fuzz_code/<target_name-minus-fuzz_-prefix>_fuzz.cpp`,
compiles and smoke-tests it locally before handing it back for review --
there's no separate drafts location or landing script to run afterward.
`src/test/CMakeLists.txt` globs every `fuzz/fuzz_code/*_fuzz.cpp` file
into its own executable, so a new harness needs nothing else registered.

Once a candidate has a harness, update its YAML's
`fuzz_code_generated_at` by hand (or ask Claude Code to do it as part of
landing the harness) so a later look at the pool shows which candidates
are still open. This isn't enforced by any script -- it's just
bookkeeping for whoever picks the next candidate to work through.

This directory used to also wire up [OSS-Fuzz-Gen](https://github.com/google/oss-fuzz-gen)
with Claude over Vertex AI to draft harnesses automatically. Removed:
the team decided to skip paid generation entirely (both this path and
fuzz_script's former Fuzz4All + Anthropic API generator, see
`../../fuzz_script/README.md`) in favor of generating everything locally
with Claude Code.
