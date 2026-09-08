// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for ExtractPSTT (src/pstt.cpp), the Extractor role's core:
// turns a decoded PartiallySignedTapyrusTransaction into a final
// CMutableTransaction, assembling each input's final_script_sig and each
// output's amount/script directly. Reachable from finalizepstt whenever
// walletprocesspstt/combinepstt report a PSTT complete. IsSane() is
// re-checked at the top of ExtractPSTT itself, but the per-input
// scriptSig/witness assembly and per-output amount handling downstream
// of that check have their own assumptions -- a natural place for a
// decoded-but-adversarial PSTT (e.g. IsSane()-passing edge cases around
// amount/sequence/locktime bounds) to reach code that assumes a
// genuinely complete, honestly-constructed PSTT.
//
// Reuses the same DecodePSTT entry point pstt_decode_fuzz.cpp fuzzes to
// turn the raw buffer into a PartiallySignedTapyrusTransaction; only
// buffers that decode successfully reach ExtractPSTT.

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
    const std::string base64_pstt(reinterpret_cast<const char*>(data), size);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, base64_pstt, error)) return 0;

    try {
        ExtractPSTT(pstt);
    } catch (const std::exception&) {
        // ExtractPSTT documents throwing std::runtime_error when
        // !pstt.IsSane() (see src/pstt.cpp) -- not a bug.
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
