// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for ParsePsttInputEntries (src/rpc/rawtransaction.cpp), the
// entry point that turns the "inputs" argument of createpstt and
// walletcreatefundedpstt from raw, RPC-client-controlled JSON into CTxIn
// objects. Follows Bitcoin Core's src/test/fuzz/deserialize.cpp convention
// (buffer -> try/parse -> discard, swallowing only the exception types the
// function is documented to throw on malformed input).

#include <tapyrus-config.h>

#include <rpc/rawtransaction.h>
#include <rpc/protocol.h>
#include <key.h>
#include <primitives/transaction.h>
#include <univalue.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <vector>

static int test_one_input_parse(const uint8_t* data, size_t size)
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

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_parse(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_parse(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
