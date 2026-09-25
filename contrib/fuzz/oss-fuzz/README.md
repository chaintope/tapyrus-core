# OSS-Fuzz tooling

Everything in this directory is part of the same fuzz_code pipeline's
relationship with Google's OSS-Fuzz ecosystem (`project/` registers
tapyrus-core as an OSS-Fuzz project; `drafting/` turns a gap-analysis
finding into a candidate spec), split into two directories by what each
half actually does.

- **[`project/`](project/)** -- the three files ([`project.yaml`](project/project.yaml),
  [`Dockerfile`](project/Dockerfile), [`build.sh`](project/build.sh)) that register
  tapyrus-core as an OSS-Fuzz project: what makes `google/oss-fuzz`'s own
  tooling (Fuzz Introspector's gap analysis today, ClusterFuzz if ever
  submitted upstream) able to build and run this repo's fuzz targets at
  all. See [`project/README.md`](project/README.md).
- **[`drafting/`](drafting/)** -- turns each function gap analysis finds
  with no fuzz coverage into a candidate YAML (name, signature, params,
  return type) under `src/test/fuzz/fuzz_candidates/`. Drafting the
  actual `fuzz_test_file` from that spec is done locally by Claude Code,
  not by an automated tool -- see [`drafting/README.md`](drafting/README.md).

`../fuzz-introspector/` (gap analysis) is the remaining piece of this
same pipeline and stays a sibling directory rather than folding in here
-- it's a distinct third-party tool with its own upstream project.
`../fuzz_script/` (the unrelated fuzz_script pipeline) is a separate
sibling directory too. `../fuzz-introspector/fuzz_code_step1_build_image.py`
is what actually consumes `project/`'s files, building `project/Dockerfile`
(which bakes in `project/build.sh`) into the local, reusable image
`../fuzz-introspector/fuzz_code_step2_start_container.py` and
`fuzz_code_step3_analyze.py` then run gap analysis against.
