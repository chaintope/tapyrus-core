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

# $CC/$CXX/$CFLAGS/$CXXFLAGS/$LIB_FUZZING_ENGINE are set by the OSS-Fuzz
# base image per the requested sanitizer/engine combination.
cmake -S . -B build_oss_fuzz \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_C_FLAGS="$CFLAGS" \
  -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
  -DSANITIZERS="${SANITIZER//,/;}" \
  -DBUILD_DAEMON=OFF -DBUILD_GUI=OFF -DBUILD_CLI=OFF \
  -DENABLE_WALLET=OFF -DENABLE_TESTS=ON -DENABLE_BENCH=OFF \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build_oss_fuzz --target fuzz_all -j "$(nproc)"

for target_path in build_oss_fuzz/bin/fuzz_*; do
  target=$(basename "${target_path}")
  cp "${target_path}" "$OUT/${target}"

  corpus_dir="qa-assets/fuzz_corpora/${target}"
  if [ -d "$corpus_dir" ]; then
    zip -rj "$OUT/${target}_seed_corpus.zip" "$corpus_dir"
  fi
done
