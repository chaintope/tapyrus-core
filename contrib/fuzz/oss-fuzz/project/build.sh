#!/bin/bash -eu
export LC_ALL=C
# Draft OSS-Fuzz build script for tapyrus-core -- see README.md in this
# directory for what this is (and isn't) used for. Unlike Bitcoin Core's
# build.sh, this uses tapyrus-core's native CMake + -DSANITIZERS build path
# directly -- no depends/ toolchain, no per-binary target-name injection
# trick.
#
# src/test/CMakeLists.txt globs every src/test/fuzz/fuzz_code/*_fuzz.cpp file into
# its own executable (target name = filename with the _fuzz.cpp suffix
# stripped and fuzz_ prepended, e.g. pstt_parse_fuzz.cpp -> fuzz_pstt_parse)
# and wires the phony fuzz_all target to depend on all of them -- landing
# a new fuzz_test_file means only adding its source file, nothing to
# register here or in .github/workflows/daily-test.yml.

# The introspector/build_fuzzers container bind-mounts the real
# tapyrus-core checkout at /src/tapyrus-core (helper.py's own
# "use a local source dir" behavior), so build_oss_fuzz/ persists on the
# host between container runs instead of vanishing with the container --
# a stale CMakeCache.txt from an earlier failed configure (e.g. a cached
# Boost_INCLUDE_DIR from before a Dockerfile fix) would otherwise silently
# survive a later, fixed image. Force a clean configure every time.
rm -rf build_oss_fuzz

# $SANITIZER also takes OSS-Fuzz's own instrumentation-mode sentinels --
# "coverage" (infra/helper.py introspector's 2nd of 3 build passes) and
# "introspector" (its 3rd) -- neither is a real Clang -fsanitize= value
# (confirmed: -fsanitize=coverage fails CMakeLists.txt's own
# CXX_SUPPORTS__FSANITIZE_COVERAGE check with "Compiler did not accept
# requested flags."). Those two modes get their actual instrumentation
# from the base image's own injected $CFLAGS/$CXXFLAGS (e.g.
# -fprofile-instr-generate -fcoverage-mapping, visible in secp256k1's own
# configure log) -- passing them through to our -DSANITIZERS, which only
# ever feeds a real -fsanitize= flag, is simply wrong. Forward $SANITIZER
# as-is for a real sanitizer build (e.g. "address"), empty otherwise.
case "$SANITIZER" in
  coverage|introspector|none|"") oss_fuzz_sanitizers="" ;;
  *) oss_fuzz_sanitizers="${SANITIZER//,/;}" ;;
esac

# base-runner (the separate, minimal image the coverage/introspector
# passes actually execute these binaries in) has none of this
# builder image's apt packages -- confirmed by hitting "error while
# loading shared libraries: libevent_extra-2.1.so.7: cannot open shared
# object file" and then grepping every script in base-runner for
# LD_LIBRARY_PATH (no matches at all: there's no mechanism to point it at
# extra .so paths). OSS-Fuzz's own convention is that fuzz binaries must
# be self-contained. Forcing every find_library() call (this repo's
# FindLibevent.cmake included) to prefer the static archive apt's -dev
# packages already ship alongside the .so (confirmed:
# /usr/lib/x86_64-linux-gnu/libevent*.a all present) needs
# CMAKE_FIND_LIBRARY_SUFFIXES=.a -- but passing that as a plain -D cache
# seed here doesn't work: confirmed directly (a minimal test project in
# this same image) that project()'s own platform/compiler-detection
# modules unconditionally `set(CMAKE_FIND_LIBRARY_SUFFIXES .so .a)` as a
# normal variable right after project() runs, shadowing our cache-seeded
# value for every find_library() call afterward (the cache entry itself
# stays .a, but nothing reads it). force_static_libs.cmake, wired in via
# CMAKE_PROJECT_Tapyrus_INCLUDE (CMake's own hook for a script
# auto-included right after the matching project() command), runs late
# enough to actually stick -- confirmed against the same minimal test.
#
# $CC/$CXX/$CFLAGS/$CXXFLAGS/$LIB_FUZZING_ENGINE are set by the OSS-Fuzz
# base image per the requested sanitizer/engine combination.
cmake -S . -B build_oss_fuzz \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$CFLAGS" \
  -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
  -DSANITIZERS="$oss_fuzz_sanitizers" \
  -DCMAKE_PROJECT_Tapyrus_INCLUDE="$SRC/force_static_libs.cmake" \
  -DBUILD_DAEMON=OFF -DBUILD_GUI=OFF -DBUILD_CLI=OFF \
  -DENABLE_WALLET=OFF -DENABLE_TESTS=ON -DENABLE_BENCH=OFF \
  -DBUILD_FUZZ_TEST=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build_oss_fuzz --target fuzz_all -j "$(nproc)"

for target_path in build_oss_fuzz/bin/fuzz_*; do
  cp "${target_path}" "$OUT/$(basename "${target_path}")"
done
