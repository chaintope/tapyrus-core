// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Shared libFuzzer/AFL entry-point boilerplate for every pstt_*_fuzz.cpp
// harness in this directory. Each harness defines its own
// `static int test_one_input(const uint8_t* data, size_t size)` and
// #includes this header at the bottom of the file, after that
// definition.
//
// LLVMFuzzerTestOneInput itself is deliberately NOT defined here even
// though every other includer of libFuzzer boilerplate would put it here
// too -- Fuzz Introspector's C/C++ frontend parses each source file's
// literal text via tree-sitter, with no preprocessing (no macro
// expansion, no #include inlining). With LLVMFuzzerTestOneInput
// previously defined once in this shared header, tree-sitter saw exactly
// one "harness" (this header) calling one `test_one_input(...)` -- but
// all 7 pstt_*_fuzz.cpp files define their own identically-named,
// internal-linkage `test_one_input`, so tree-sitter's non-preprocessing,
// syntax-only symbol resolution had no way to tell which file's
// definition that one call site should bind to. The whole call graph
// dead-ended there, so Fuzz Introspector's own per-function reachability
// analysis found zero real profiles for anything (confirmed directly:
// `project.get_source_codes_with_harnesses()` returned exactly one
// harness, this header file, not any of the 7 real target files).
// Each .cpp file now defines its own literal LLVMFuzzerTestOneInput
// immediately after its own test_one_input, in the same file/scope --
// same-file symbol resolution needs no preprocessing understanding.
//
// That alone wasn't sufficient, though (confirmed against a real
// analysis run: every harness's calltree showed the SAME 7 downstream
// functions, and ParsePsttInputEntries -- the whole point of
// pstt_parse_fuzz.cpp -- reported is_reached=false). The ambiguity just
// moved one hop deeper: `static int test_one_input(...)` is STILL
// identically named across all 7 files, so each file's own
// LLVMFuzzerTestOneInput calling generic `test_one_input(data, size)`
// hit the exact same cross-file symbol-resolution problem again --
// tree-sitter picked one arbitrary file's test_one_input definition to
// represent the callee at all 7 call sites (verified: pstt_parse_fuzz's
// own calltree showed its LLVMFuzzerTestOneInput/test_one_input calling
// into pstt_locktime_fuzz.cpp's body). Each .cpp file therefore also
// gives its real logic a second, uniquely-named function
// (test_one_input_<target>) that LLVMFuzzerTestOneInput calls directly;
// the generic `test_one_input` name is kept only as a thin one-line
// forwarder to that unique function, solely so this header's own
// main()/AFL path below (which has no per-file context to call a unique
// name with) still has something valid to call. main() is a weak symbol
// only used for a non-libFuzzer AFL/stdin build; fuzz-introspector never
// treats it as a harness entrypoint, so this residual, intentional
// ambiguity in its own call graph doesn't affect gap analysis.

#ifndef TAPYRUS_TEST_FUZZ_FUZZ_CODE_PSTT_FUZZ_DRIVER_H
#define TAPYRUS_TEST_FUZZ_FUZZ_CODE_PSTT_FUZZ_DRIVER_H

#include <key.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unistd.h>
#include <vector>

static bool read_stdin(std::vector<uint8_t>& data)
{
    uint8_t buffer[1024];
    ssize_t length = 0;
    while ((length = read(STDIN_FILENO, buffer, 1024)) > 0) {
        data.insert(data.end(), buffer, buffer + length);
        if (data.size() > (1 << 20)) return false;
    }
    return length == 0;
}

static std::unique_ptr<ECCVerifyHandle> globalVerifyHandle;
void initialize()
{
    globalVerifyHandle = std::make_unique<ECCVerifyHandle>();
}

// This function is used by libFuzzer.
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    initialize();
    return 0;
}

// Disabled under WIN32 due to clash with Cygwin's WinMain.
#ifndef WIN32
// Declare main(...) "weak" to allow for libFuzzer linking. libFuzzer provides
// the main(...) function.
__attribute__((weak))
#endif
int main(int argc, char** argv)
{
    initialize();
#ifdef __AFL_INIT
    // Enable AFL deferred forkserver mode. Requires compilation using
    // afl-clang-fast++. See doc/fuzzing.md for details.
    __AFL_INIT();
#endif

#ifdef __AFL_LOOP
    // Enable AFL persistent mode. Requires compilation using afl-clang-fast++.
    // See doc/fuzzing.md for details.
    int ret = 0;
    while (__AFL_LOOP(1000)) {
        std::vector<uint8_t> buffer;
        if (!read_stdin(buffer)) continue;
        ret = test_one_input(buffer.data(), buffer.size());
    }
    return ret;
#else
    std::vector<uint8_t> buffer;
    if (!read_stdin(buffer)) return 0;
    return test_one_input(buffer.data(), buffer.size());
#endif
}

#endif // TAPYRUS_TEST_FUZZ_FUZZ_CODE_PSTT_FUZZ_DRIVER_H
