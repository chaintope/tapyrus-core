# OSS-Fuzz project definition (draft)

This directory is a draft of the three files an OSS-Fuzz project submission
needs (`project.yaml`, `Dockerfile`, `build.sh`), modeled on Bitcoin Core's
real, currently-active submission at
`google/oss-fuzz/projects/bitcoin-core/`. It serves two different purposes
that are easy to conflate:

1. **Eventual upstream submission.** If tapyrus-core ever wants its fuzz
   targets running continuously on Google's ClusterFuzz infrastructure (the
   actual value OSS-Fuzz provides -- distributed, always-on fuzzing with
   automatic bug filing), these three files are what a PR to
   `google/oss-fuzz` adding `projects/tapyrus-core/` would contain. That's a
   separate submission process requiring upstream review/acceptance -- not
   something achievable by adding files to this repo alone. Not filed yet.
2. **A reference build recipe.** Independent of upstream submission, these
   files document a known-working way to build tapyrus-core's fuzz targets
   with sanitizers, for anyone (including our own CI jobs -- see the
   `fuzz-code-only` job in `.github/workflows/daily-test.yml`) who wants
   to reproduce an OSS-Fuzz-style build locally.

Unlike Bitcoin Core's `build.sh` (which uses the old `depends/` +
autotools toolchain and a "binary injection" trick to pack many targets into
one compiled binary), tapyrus-core's modern CMake build already supports
sanitizer instrumentation directly via `-DSANITIZERS=...` (see
`CMakeLists.txt`'s `sanitize_interface`), and each fuzz target is its own
`add_executable(...)` (see `src/test/fuzz/fuzz_code/pstt_parse_fuzz.cpp`'s CMake
wiring) -- no per-target binary-packing trick needed. `build.sh` below
reflects that simpler, native path rather than copying Bitcoin Core's.

This directory is also the input to a third purpose that lives outside
it: `../fuzz-introspector/fuzz_code_run_introspector.py` copies these
three files into a cloned `google/oss-fuzz` checkout to register
tapyrus-core as a project there, then runs that checkout's own
`infra/helper.py introspector` -- which builds this project via
`build.sh` inside a container based on `Dockerfile` -- to find
functions with no fuzz coverage. That's the actual reason this
directory has to exist and stay buildable today, independent of
whether it's ever submitted upstream.

`build.sh` builds the phony `fuzz_all` CMake target and then copies
whatever executables land under `build_oss_fuzz/bin/fuzz_*` -- since
`src/test/CMakeLists.txt` globs every `src/test/fuzz/fuzz_code/*_fuzz.cpp` file
into its own executable and wires `fuzz_all` to depend on all of them,
landing a new fuzz_test_file (see `../drafting/fuzz_code_generate_and_draft.py`
and `../fuzz-introspector/`) already covers this file too -- nothing here
needs editing when the target list grows.
