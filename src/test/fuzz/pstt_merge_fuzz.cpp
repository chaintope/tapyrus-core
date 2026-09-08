// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for PartiallySignedTapyrusTransaction::Merge (src/pstt.cpp),
// the core of combinepstt: takes two independently-supplied PSTTs (as
// separate RPC-client-controlled base64 strings in the real RPC) and
// merges per-input/per-output signature data, xpubs, and unknown fields.
// Unlike DecodePSTT (fuzzed separately by pstt_decode_fuzz.cpp), Merge
// operates on two already-parsed objects and has its own cross-object
// invariants (matching input/output counts) rather than wire-format
// ones -- a natural target for corrupting two otherwise-valid PSTTs
// into a combination that trips something Merge()/PSTTInput::Merge()/
// PSTTOutput::Merge() didn't anticipate.
//
// The fuzz buffer is split into two halves at its midpoint, each half
// base64-decoded independently via the same DecodePSTT entry point
// pstt_decode_fuzz.cpp fuzzes; Merge is only invoked when both halves
// decode successfully, since Merge() itself does not parse wire bytes.

#include <tapyrus-config.h>

#include <core_io.h>
#include <key.h>
#include <pstt.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
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

static int test_one_input(const uint8_t* data, size_t size)
{
    const size_t mid = size / 2;
    const std::string first(reinterpret_cast<const char*>(data), mid);
    const std::string second(reinterpret_cast<const char*>(data) + mid, size - mid);

    PartiallySignedTapyrusTransaction pstt1;
    PartiallySignedTapyrusTransaction pstt2;
    std::string error;
    if (!DecodePSTT(pstt1, first, error)) return 0;
    if (!DecodePSTT(pstt2, second, error)) return 0;

    try {
        pstt1.Merge(pstt2);
    } catch (const std::exception&) {
        // Merge()/PSTTInput::Merge()/PSTTOutput::Merge() document throwing
        // on mismatched input/output counts or incompatible per-entry
        // state (see src/pstt.cpp) -- not a bug.
    }
    return 0;
}

static std::unique_ptr<ECCVerifyHandle> globalVerifyHandle;
void initialize()
{
    globalVerifyHandle = std::make_unique<ECCVerifyHandle>();
}

// This function is used by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input(data, size);
    return 0;
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
