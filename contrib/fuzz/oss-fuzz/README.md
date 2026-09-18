# OSS-Fuzz tooling

Everything in this directory is part of the same fuzz_code pipeline's
relationship with Google's OSS-Fuzz ecosystem, split into two
directories by what each half actually does -- not by which upstream
tool wrote it, which is why `oss-fuzz` and `oss-fuzz-gen` (two
separately-named upstream projects) live under one directory here.

- **[`project/`](project/)** -- the three files ([`project.yaml`](project/project.yaml),
  [`Dockerfile`](project/Dockerfile), [`build.sh`](project/build.sh)) that register
  tapyrus-core as an OSS-Fuzz project: what makes `google/oss-fuzz`'s own
  tooling (Fuzz Introspector's gap analysis today, ClusterFuzz if ever
  submitted upstream) able to build and run this repo's fuzz targets at
  all. See [`project/README.md`](project/README.md).
- **[`drafting/`](drafting/)** -- the pipeline that finds functions with
  no fuzz coverage and has [OSS-Fuzz-Gen](https://github.com/google/oss-fuzz-gen)
  (a separate Google tool, despite the similar name) draft a
  `fuzz_test_file` for them via Claude. See [`drafting/README.md`](drafting/README.md).

`../fuzz-introspector/` (gap analysis) is the remaining piece of this
same pipeline and stays a sibling directory rather than folding in here
-- it's a distinct third-party tool with its own upstream project, the
same reasoning that keeps `../fuzz4all/` (the unrelated fuzz_script
pipeline) separate too. `../fuzz-introspector/fuzz_code_run_introspector.py`
is what actually consumes `project/`'s files, copying them into a cloned
`google/oss-fuzz` checkout before running Introspector.
