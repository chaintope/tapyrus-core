// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for the real decodepstt RPC handler (src/rpc/rawtransaction.cpp),
// as opposed to pstt_decode_fuzz.cpp's direct call to DecodePSTT alone.
// decodepstt is the one call site that reaches EstimatePsttFee and
// EstimateInputScriptSig (both `static` in rawtransaction.cpp, so not
// independently linkable from a separate fuzz executable) -- both were
// the subject of real, previously-fixed bugs (PSTTImplementation5 review
// findings #2/#3: EstimatePsttFee dereferencing an unset
// boost::optional<CAmount>, EstimateInputScriptSig indexing
// input.utxo->vout.at(prevout_index) unchecked). decodepstt also runs
// the dry-run SignPSTTInput-against-DUMMY_SIGNING_PROVIDER "next"-role
// computation pstt_signer_fuzz.cpp fuzzes directly, so this target's
// real value is the two Estimate* functions and decodepstt's own
// UniValue-assembly code, not re-covering SignPSTTInput itself.
//
// decodepstt has no chain/wallet dependency (confirmed by reading its
// full body: no pwallet/ChainActive/GetTransaction/mempool use), so it's
// callable here with only a bare JSONRPCRequest wrapping the fuzzed
// string as request.params[0] -- exactly the one argument real RPC
// clients send.

#include <tapyrus-config.h>

#include <core_io.h>
#include <key.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <univalue.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

extern UniValue decodepstt(const JSONRPCRequest& request);

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

    JSONRPCRequest request;
    request.params = UniValue(UniValue::VARR);
    request.params.push_back(base64_pstt);

    try {
        decodepstt(request);
    } catch (const UniValue&) {
        // JSONRPCError(...) is thrown by value as a UniValue -- the
        // documented way decodepstt reports a decode/sanity failure
        // (see src/rpc/rawtransaction.cpp). Not a bug.
    } catch (const std::exception&) {
        // UniValue's own get_str()/type checks throw std::runtime_error
        // on an unexpected shape; not reachable here since params[0] is
        // always a string, but kept for the same reason
        // pstt_parse_fuzz.cpp keeps it.
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
