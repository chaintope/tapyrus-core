// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for ParsePsttInputEntries (src/rpc/rawtransaction.cpp), the
// entry point that turns the "inputs" argument of createpsttfromtransaction
// et al. from raw, RPC-client-controlled JSON into CTxIn objects. Drafted
// with the help of Fuzz Introspector-style analysis of the target function
// signature/call graph and Claude, following Bitcoin Core's
// src/test/fuzz/deserialize.cpp convention (buffer -> try/parse -> discard,
// swallowing only the exception types the function is documented to throw
// on malformed input) rather than test_tapyrus_fuzzy.cpp's older
// stdin/TEST_ID-switch style.

#include <tapyrus-config.h>

#include <rpc/rawtransaction.h>
#include <rpc/protocol.h>
#include <key.h>
#include <primitives/transaction.h>
#include <univalue.h>

#include <test/fuzz/FuzzedDataProvider.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
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
    FuzzedDataProvider fdp(data, size);

    // nLockTime/rbfOptIn steer which branch of the not-otherwise-nested
    // nSequence default computation runs; the remainder of the buffer is the
    // JSON text itself, since that's the field with actual parsing depth.
    const uint32_t nLockTime = fdp.ConsumeIntegral<uint32_t>();
    const bool rbfOptIn = fdp.ConsumeBool();
    const std::string json = fdp.ConsumeRemainingBytesAsString();

    UniValue inputs_in;
    if (!inputs_in.read(json)) return 0;

    try {
        ParsePsttInputEntries(inputs_in, nLockTime, rbfOptIn);
    } catch (const UniValue&) {
        // JSONRPCError(...) is thrown by value as a UniValue -- the
        // documented way this function reports a malformed "vout"/
        // "sequence"/out-of-range entry. Not a bug.
    } catch (const std::exception&) {
        // UniValue::get_array()/get_obj()/get_int()/get_int64() throw
        // UniValue::type_error (a std::runtime_error) on a wrong-shaped
        // JSON value (e.g. "inputs" not an array, an entry not an object).
        // Also not a bug.
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
