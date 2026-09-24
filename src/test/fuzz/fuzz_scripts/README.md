# Fuzz4All-generated candidate pool

AI-generated candidate Script programs -- one file per candidate,
filenames prefixed with their generation run's timestamp and model.
Fuzz4All has only ever targeted Tapyrus Script here, so this directory
holds that pool directly rather than nesting it under a per-target
subdirectory. `daily-test.yml`'s `fuzz-script-sweep` job tests a
rotating, time-bounded window of this pool against `tapyrus-verify
--fuzz` every day: each run starts at a different offset (keyed off the
workflow's own run number) and walks forward until either its time
budget runs out or it laps back to its starting point, so successive
runs gradually cover the whole pool instead of always retesting the
same prefix.

Unlike `src/test/fuzz/fuzz_seed_pool/`, these are not mutation seeds -- there is
no libFuzzer engine here. Each candidate is fed to `tapyrus-verify
--fuzz` directly and unchanged.

The pool grows via `contrib/fuzz/fuzz4all/fuzz_script_generate_pool.py`, a
human-run local script (never a CI job, run with your own
`ANTHROPIC_API_KEY`) that alternates Haiku 4.5/Sonnet 5. There is no
shared budget cap -- whoever runs it watches their own account for
spend, and `--max-candidates` bounds a single run. Every candidate lands
directly in this directory, so review is a prune, not a landing step --
see `contrib/fuzz/fuzz4all/README.md`'s "Human review, not auto-commit"
section for the `review_scripts_<run-prefix>.html` +
`fuzz_script_land_approved.py` workflow that decides what actually stays
committed.
