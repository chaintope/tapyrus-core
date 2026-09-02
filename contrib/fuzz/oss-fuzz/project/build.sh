#!/bin/bash -eu
export LC_ALL=C
# Draft OSS-Fuzz build script for tapyrus-core -- see README.md in this
# directory for what this is (and isn't) used for. Unlike Bitcoin Core's
# build.sh, this uses tapyrus-core's native CMake + -DSANITIZERS build path
# directly -- no depends/ toolchain, no per-binary target-name injection
# trick. Each entry in FUZZ_TARGETS is both the CMake target name and the
# output binary name OSS-Fuzz expects under $OUT.

# Read from the same file .github/workflows/daily-test.yml's fuzz-code-only
# job reads -- one canonical list of fuzz_test_file target names, so a new
# fuzz_test_file only needs to be added there once. See
# src/test/fuzz/FUZZ_TARGETS.txt for what else landing one needs (a CMake
# target block, not just this list).
mapfile -t FUZZ_TARGETS < <(grep -v '^#' src/test/fuzz/FUZZ_TARGETS.txt | grep -v '^[[:space:]]*$')

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

for target in "${FUZZ_TARGETS[@]}"; do
  cmake --build build_oss_fuzz --target "${target}" -j "$(nproc)"
  cp "build_oss_fuzz/bin/${target}" "$OUT/${target}"

  corpus_dir="qa-assets/fuzz_corpora/${target}"
  if [ -d "$corpus_dir" ]; then
    zip -rj "$OUT/${target}_seed_corpus.zip" "$corpus_dir"
  fi
done
