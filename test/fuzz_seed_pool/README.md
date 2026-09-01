# Fuzz seed pools

One subdirectory per libFuzzer target (matching `FUZZ_TARGETS` in the
`fuzz-code-only` job of `.github/workflows/daily-test.yml`), each holding a
large pool of seed inputs -- one file per seed. That job rotates a
different slice of each pool into that target's working corpus every run
via `contrib/fuzz/fuzz_code_select_slice.py`, so consecutive runs
anchor their mutation exploration on different seeds instead of always
starting from the same handful.

These are seeds, not a fixed test suite: libFuzzer still mutates outward
from whatever slice is selected for the whole run's time budget, so actual
coverage explored each run goes well beyond the seed files themselves.

To grow a pool: take genuinely interesting inputs (new coverage, found a
bug, exercises an edge case a unit test doesn't) from a target's
`new_seed_candidates/<target>/` run artifact, review them, and commit the
ones worth keeping into `<target>/` here. Not auto-committed from CI --
see this repo's working agreement on generated/discovered content going
through human review before landing.
