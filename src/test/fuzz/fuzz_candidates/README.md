# Candidate pool

One YAML file per candidate function (written by
`contrib/fuzz/oss-fuzz/drafting/fuzz_code_generate_candidates.py`, fed by
Fuzz Introspector's gap analysis) -- each describing one function that
has no fuzz coverage yet: name, signature, params, return type. (The
YAML content itself uses OSS-Fuzz-Gen's own name for this file format,
"benchmark" -- see `fuzz_code_generate_candidates.py`'s module
docstring for why. We call the pool and the generation step "candidate"
because that's what each entry is from our side: a candidate function
waiting on a fuzz_test_file.)

This pool grows via `contrib/fuzz/oss-fuzz/drafting/fuzz_code_generate_candidates.py`,
run by hand occasionally (`--limit`, default 5 new entries per
invocation) and committed here after human review -- not automatic, same
working agreement as the other two pools in this repo
(`src/test/fuzz/fuzz_seed_pool/`, `src/test/fuzz/fuzz_scripts/`). There
is no CI workflow that touches this pool at all -- see the note in
`contrib/fuzz/oss-fuzz/drafting/README.md` about why gap analysis and
candidate generation stayed out of GitHub Actions.

Unlike those two pools, entries here aren't consumed by running a
fuzz_test_file -- they're consumed by *writing* one. Turning a candidate
into an actual fuzz_test_file means asking Claude Code to draft it
directly, using the candidate's own YAML as the spec, then writing the
result straight under `src/test/fuzz/fuzz_code/` -- no automated
drafting tool, no paid API call, no separate drafts location to land
from afterward (see `contrib/fuzz/oss-fuzz/drafting/README.md`).

A drafted fuzz_test_file still needs the same human review as everything
else here before it becomes a real fuzz target (see `daily-test.yml`'s
`fuzz-code-only` job, which builds and runs one executable per
`src/test/fuzz/fuzz_code/*_fuzz.cpp` file). Writing a candidate's harness
doesn't remove it from this pool automatically; that's a manual cleanup
step (updating the candidate's own `fuzz_code_generated_at`, see below)
once a maintainer has actually looked at the result.

Every candidate YAML carries two dates, so its state is visible without
opening any other file: `yaml_generated_at` (when this candidate first
entered the pool) and `fuzz_code_generated_at` (null until a harness has
actually been drafted for it -- set by hand, since drafting is no longer
an automated step). `fuzz_gaps.json` itself carries a matching top-level
`generated_at`, so comparing it against the pool's own
`yaml_generated_at`/`fuzz_code_generated_at` values tells you whether
it's worth re-running gap analysis (`fuzz_code_step1_build_image.py`
through `fuzz_code_find_fuzz_gaps.py`) again before drafting, or whether
the existing pool is still current.
