// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for TokenAmountFromValue (src/rpc/server.cpp): parses an
// RPC-client-controlled JSON number/string into a token-count CAmount,
// rejecting non-numeric/non-string input and out-of-range values.

#include <tapyrus-config.h>

#include <rpc/protocol.h>
#include <rpc/server.h>
#include <univalue.h>

#include <cstdint>
#include <string>
#include <unistd.h>

static int test_one_input_tokenamountfromvalue(const uint8_t* data, size_t size)
{
    const std::string json(reinterpret_cast<const char*>(data), size);

    UniValue value;
    if (!value.read(json)) return 0;

    try {
        TokenAmountFromValue(value);
    } catch (const UniValue&) {
        // JSONRPCError(...) is thrown by value as a UniValue -- the
        // documented way this function reports a non-numeric or
        // out-of-range amount. Not a bug.
    }
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_tokenamountfromvalue(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_tokenamountfromvalue(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
