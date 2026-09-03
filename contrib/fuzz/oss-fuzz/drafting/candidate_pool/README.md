# Candidate pool

One YAML file per candidate function (written by
`contrib/fuzz/oss-fuzz/drafting/fuzz_code_generate_candidates.py`, fed by Fuzz Introspector's
gap analysis) -- each describing one function OSS-Fuzz-Gen should write a
fuzz_test_file for. (The YAML content itself uses OSS-Fuzz-Gen's own name
for this file format, "benchmark" -- see `fuzz_code_generate_candidates.py`'s module
docstring for why. We call the pool and the generation step "candidate"
because that's what each entry is from our side: a candidate function
waiting on a fuzz_test_file.)

This pool grows via `../fuzz_code_generate_and_draft.py`, run by hand
occasionally (`--gap-candidate-limit`, default 100 new entries per
invocation) and committed here after human review -- not automatic, same
working agreement as the other two pools in this repo
(`test/fuzz_seed_pool/`, `contrib/fuzz/fuzz4all/generated_pool/`). There
is no CI workflow that touches this pool at all -- see the note in
`../README.md` about why gap analysis, candidate generation, and
drafting all stayed out of GitHub Actions.

Unlike those two pools, entries here aren't consumed by running a
fuzz_test_file -- they're consumed by *writing* one. The same local
script that grows this pool also drafts a batch from it via
OSS-Fuzz-Gen, one candidate at a time, stopping once
`../fuzz_spend_ledger.py`'s shared monthly cap is (estimated to be)
spent -- shared with `../fuzz4all/fuzz_script_generate_pool.py`, so a
drafting run here can leave less budget for that script's generation
this month, and vice versa. `--draft-limit` (default 100000) is still an
independent upper bound if you want one, but in practice the shared
budget estimate is what stops a run. Deliberately capped rather than
draining the whole pool in one run: OSS-Fuzz-Gen's own
fuzz_test_file-writing loop spends real LLM tokens per candidate, so a
maintainer runs the drafting stage again whenever they want to work
through more of the backlog, rather than paying for all of it in one
sitting.

A written fuzz_test_file that comes out the other end still needs the
same human review as everything else here before it becomes a real fuzz
target (see `daily-test.yml`'s `fuzz-code-only` job and its
`FUZZ_TARGETS` list) -- that review happens via the `review_code.html` page
`fuzz_code_generate_and_draft.py` builds from the drafts and
`../fuzz_code_land_approved.py` lands from your saved copy of it, see
`../README.md`'s "Human review, not auto-merge" section. Writing a
candidate here doesn't remove it from the pool automatically; that's a
manual cleanup step once a maintainer has actually looked at the result.
