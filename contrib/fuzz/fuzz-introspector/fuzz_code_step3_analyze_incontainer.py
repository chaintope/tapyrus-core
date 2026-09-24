#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Runs *inside* the persistent container fuzz_code_step2_start_container.py
started -- fuzz_code_step3_analyze.py execs this via `docker exec`, using
the fact that the container bind-mounts the live tapyrus-core checkout
(this file's own path included) rather than needing to COPY it into the
image. Not meant to be run on the host.

Two things happen here, both every time this runs (see fuzz_code_step1_
build_image.py and this pipeline's own design discussion for why neither
is optional):

1. A real coverage-sanitizer build via /usr/local/bin/compile (the base
   image's own env-setup wrapper around build.sh -- reused deliberately
   rather than reimplemented, since it already gets $CC/$CXX/$CFLAGS/
   $CXXFLAGS exactly right per SANITIZER_FLAGS_coverage/COVERAGE_FLAGS_*,
   baked into the base image itself). Produces real compiled+instrumented
   fuzz binaries under $OUT.
2. Fuzz Introspector's own Python API, called directly rather than via
   its CLI wrapper (skipping infra/helper.py's per-language conditionals
   entirely, none of which apply to us):
   - analyse_end_to_end(): the tree-sitter-based source analysis that was
     silently producing zero profiles before this pipeline's harness fix
     (see src/test/fuzz/fuzz_code/pstt_fuzz_driver.h's own comment) --
     confirmed fixed directly against this project's real source tree.
   - correlate_binaries_to_logs(): a static scan of the just-built
     binaries (no execution needed -- confirmed by reading its actual
     implementation, it only inspects the compiled ELFs).

Writes analysis output under /out/inspector (host-visible via step 2's
bind mount) for fuzz_code_step3_analyze.py to hand to the existing
fuzz_code_find_fuzz_gaps.py afterward, unchanged.

Fuzz Introspector's own analysis (step 2 below) runs against a filtered
COPY of the source tree, not the real checkout directly -- see
build_scoped_source_tree()'s own docstring for why. The coverage build
(step 1) still compiles the real checkout as-is; only the source-text
analysis is scoped down.

--analysis-only skips step 1 (the coverage build) entirely -- confirmed
that analyse_end_to_end() is pure tree-sitter source-text analysis with
no dependency on freshly-built binaries (only correlate_binaries_to_logs()
reads binaries, and those already persist under $OUT from a prior run's
step 1). For this script's actual intended use -- "source changed,
re-rank gaps" -- skipping a real ~10+ minute rebuild every single
re-analysis is the single biggest speed win available. First run in a
container still needs the full path (no binaries exist yet).
"""
import argparse
import os
import shutil
import subprocess
import sys

PROJECT_SRC = "/src/tapyrus-core"
OUT_DIR = "/out"
INSPECTOR_OUT_DIR = "/out/inspector"
SCOPED_SRC = "/tmp/scoped_src"

# Vendored third-party libraries, plus this project's own GUI/bench code,
# under src/ -- confirmed via fuzz_code_find_fuzz_gaps.py's own
# UNTRUSTED_INPUT_DIRS filter that none of these can ever produce a
# candidate function anyway (that filter only considers src/rpc,
# src/script, src/primitives, src/net_processing.cpp, src/policy), so
# scanning them at all only spends memory/time on source text whose
# analysis result nothing downstream will ever look at. src/qt/ alone is
# 347 of ~451 scoped files before this exclusion (77%) -- confirmed by
# direct count -- despite this pipeline never building the GUI at all
# (no qt in build.sh's cmake invocation). src/wallet/ is real tapyrus-core
# code (not vendored/GUI) and stays in scope even though
# UNTRUSTED_INPUT_DIRS doesn't currently rank anything under it -- that's
# a statement about today's ranking filter, not a reason to exclude the
# source from analysis entirely.
VENDORED_DIRS = {"leveldb", "secp256k1", "univalue"}
IRRELEVANT_PROJECT_DIRS = {"qt", "bench"}


def run_coverage_build() -> None:
    print("== 1/2: coverage-sanitizer build (via /usr/local/bin/compile) ==", flush=True)
    env = dict(os.environ)
    env.update({
        "SANITIZER": "coverage",
        "FUZZING_ENGINE": "libfuzzer",
        "ARCHITECTURE": "x86_64",
        "PROJECT_NAME": "tapyrus-core",
        "FUZZING_LANGUAGE": "c++",
        "HELPER": "True",
        "OUT": OUT_DIR,
    })
    subprocess.run(["bash", "-eux", "/usr/local/bin/compile"], check=True, cwd=PROJECT_SRC, env=env)


def _ignore_for_scoping(dir_path: str, names: list) -> set:
    ignored = {n for n in names if n in VENDORED_DIRS or n in IRRELEVANT_PROJECT_DIRS}
    # src/test/ holds this project's own unit tests (test_*.cpp etc, none
    # of which are FUZZ_TARGET-style LLVMFuzzerTestOneInput harnesses --
    # Fuzz Introspector's entrypoint search would never treat them as
    # one), except src/test/fuzz/fuzz_code/, which holds the real
    # harnesses and must stay in scope.
    if os.path.basename(dir_path) == "test" and os.path.basename(os.path.dirname(dir_path)) == "src":
        ignored |= {n for n in names if n != "fuzz"}
    return ignored


def build_scoped_source_tree() -> str:
    """Fresh filtered copy of PROJECT_SRC/src, excluding VENDORED_DIRS and
    non-fuzz src/test/ content -- see this module's own docstring and
    VENDORED_DIRS' comment for why. A physical copy (not e.g. a curated
    file list handed straight to fuzz_introspector's own frontend
    functions) so Fuzz Introspector's existing directory-walking logic
    (oss_fuzz.capture_source_files_in_tree, called internally by
    analyse_folder/analyse_end_to_end) does the filtering for free just by
    being pointed at a smaller tree -- no need to understand or replicate
    its internal light-mode data format to feed it a pre-built file list
    instead.

    Rebuilt from scratch every run (rm -rf first) so a source file
    deleted from the real checkout can't linger here from an earlier run.
    """
    print(f"Building scoped source tree at {SCOPED_SRC} (excluding {sorted(VENDORED_DIRS)} "
          "and non-fuzz src/test/ content)...", flush=True)
    shutil.rmtree(SCOPED_SRC, ignore_errors=True)
    shutil.copytree(os.path.join(PROJECT_SRC, "src"), os.path.join(SCOPED_SRC, "src"),
                     ignore=_ignore_for_scoping)
    return SCOPED_SRC


def run_introspector_analysis() -> None:
    print("== 2/2: Fuzz Introspector analysis (full + correlate) ==", flush=True)
    os.makedirs(INSPECTOR_OUT_DIR, exist_ok=True)
    scoped_src = build_scoped_source_tree()
    from fuzz_introspector import commands

    # analyse_end_to_end() calls run_analysis_on_dir(enable_all_analyses=True)
    # internally (not something this call can override), which runs every
    # registered analyser -- including FrontendAnalyser
    # (analyses/frontend_analyser.py), whose analysis_func() runs a SECOND,
    # independent oss_fuzz.analyse_folder() pass against
    # os.environ.get('SRC', '/src') directly, ignoring target_dir/scoped_src
    # entirely. In this container $SRC=/src, the OSS-Fuzz convention
    # covering not just this project but also /src/aflplusplus,
    # /src/honggfuzz, /src/libfuzzer, /src/fuzztest, and the REAL,
    # unscoped /src/tapyrus-core (leveldb/secp256k1/univalue and all) --
    # confirmed directly (`echo $SRC` in this image) and confirmed as the
    # actual cause of multiple real runs continuing to consume CPU/memory
    # and eventually enormous disk I/O for hours after their own real
    # output (result.json/summary.json/calltree files) had already been
    # written to disk. Overriding SRC for the duration of this call scopes
    # that second pass down to the same filtered tree as the first.
    old_src = os.environ.get("SRC")
    os.environ["SRC"] = scoped_src
    try:
        exit_code, values = commands.analyse_end_to_end(
            arg_language="c++",
            target_dir=scoped_src,
            entrypoint="LLVMFuzzerTestOneInput",
            out_dir=INSPECTOR_OUT_DIR,
            report_name="tapyrus-core",
        )
    finally:
        if old_src is None:
            os.environ.pop("SRC", None)
        else:
            os.environ["SRC"] = old_src
    print(f"analyse_end_to_end: exit_code={exit_code}, keys={list(values.keys())}", flush=True)

    correlate_exit = commands.correlate_binaries_to_logs(OUT_DIR)
    print(f"correlate_binaries_to_logs: exit_code={correlate_exit}", flush=True)

    # Confirmed against a real successful run: analyse_end_to_end()'s own
    # return dict only ever carries a 'light-project' key (its own
    # return_values['light-project'] = project assignment, see
    # commands.py's analyse_end_to_end -- nothing named 'return_values' or
    # 'project' is ever a key of the dict itself). The original version of
    # this check keyed off both of those names and so fired every time,
    # including on full success -- fixed to check the key that's actually
    # there.
    if "light-project" not in values:
        print("WARNING: analyse_end_to_end did not report even light-mode project "
              "structure -- check the log above for 'Found data issues' or similar. "
              "fuzz_gaps.json may end up empty.", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--analysis-only", action="store_true",
                         help="Skip the coverage-sanitizer build (step 1) and reuse whatever "
                              "binaries already exist under $OUT from a prior run. Use for the "
                              "'source changed, re-rank gaps' loop this script is built for -- "
                              "the analysis itself doesn't need freshly-built binaries at all, "
                              "only correlate_binaries_to_logs() touches $OUT, and it just "
                              "reuses what's already there. First run in a fresh container still "
                              "needs a real build first (no binaries exist yet).")
    args = parser.parse_args()

    if not args.analysis_only:
        run_coverage_build()
    else:
        print("== 1/2: skipped (--analysis-only) -- reusing existing binaries under "
              f"{OUT_DIR} ==", flush=True)
    run_introspector_analysis()
    print(f"Done. Analysis output under {INSPECTOR_OUT_DIR} (host-visible via step 2's /out mount).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
