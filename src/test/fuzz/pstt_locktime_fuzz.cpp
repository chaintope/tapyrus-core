// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for ComputeLocktime and
// PartiallySignedTapyrusTransaction::GetIdentifier (both src/pstt.cpp) --
// the two remaining functions in the module with real per-PSTT logic
// not already exercised by pstt_merge_fuzz.cpp/pstt_extract_fuzz.cpp/
// pstt_signer_fuzz.cpp/pstt_decodepstt_rpc_fuzz.cpp. Called directly and
// separately (not just via GetIdentifier, which calls ComputeLocktime
// internally) so a ComputeLocktime-only regression is directly
// attributable rather than only visible through GetIdentifier's own
// behavior.
//
// ComputeLocktime reconciles each input's required_height_locktime/
// required_time_locktime constraints (max-of-constraints per kind,
// height preferred when both are simultaneously satisfiable, refused
// outright when they conflict) into one locktime for the whole PSTT --
// pure arithmetic over already-decoded fields, no exceptions of its own.
// GetIdentifier builds on it to materialize an all-sequences-zeroed
// transaction and hash it, documented to throw std::runtime_error when
// ComputeLocktime finds no valid locktime.

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

    uint32_t locktime_out = 0;
    ComputeLocktime(pstt, locktime_out);

    try {
        pstt.GetIdentifier();
    } catch (const std::exception&) {
        // Documented to throw std::runtime_error when ComputeLocktime
        // finds no valid locktime (see src/pstt.cpp) -- not a bug.
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
