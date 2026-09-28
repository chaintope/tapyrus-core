# fuzz_script candidate pool

AI-generated candidate Script programs -- one file per candidate,
filenames prefixed with their generation date. `daily-test.yml`'s
`fuzz-script-sweep` job tests a rotating, time-bounded window of this
pool against `tapyrus-verify --fuzz` every day: each run starts at a different offset (keyed off the
workflow's own run number) and walks forward until either its time
budget runs out or it laps back to its starting point, so successive
runs gradually cover the whole pool instead of always retesting the
same prefix.

Unlike `src/test/fuzz/fuzz_seed_pool/`, these are not mutation seeds -- there is
no libFuzzer engine here. Each candidate is fed to `tapyrus-verify
--fuzz` directly and unchanged.

The pool grows locally, by hand: Claude Code writes a batch of
candidates straight into this directory in an interactive session (no
paid API call, no `ANTHROPIC_API_KEY`), and
`contrib/fuzz/fuzz_script/fuzz_script_step2_validate.py` checks them
against a local sanitizer build of `tapyrus-verify`, deleting any the
assembler rejects. What's left is reviewed with `git status`/`git diff`
and committed by hand -- see `contrib/fuzz/fuzz_script/README.md`.
