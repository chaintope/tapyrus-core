// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for ConstructTransaction (src/rpc/rawtransaction.cpp): turns
// the user-supplied inputs/outputs/locktime/replaceable arguments of
// createrawtransaction (and friends) into a CMutableTransaction. The fuzz
// input is one JSON array whose first four elements are passed as those
// four arguments; missing elements are passed as null. No node context
// required, but address decoding needs the chain parameters selected.

#include <tapyrus-config.h>

#include <chainparams.h>
#include <federationparams.h>
#include <primitives/transaction.h>
#include <rpc/rawtransaction.h>
#include <tapyrusmodes.h>
#include <univalue.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unistd.h>

// DecodeDestination() reads the base58/bech32 prefixes from the global
// chain parameters, which must be selected once before first use.
class ChainParamsSelection
{
public:
    ChainParamsSelection()
    {
        SelectParams(TAPYRUS_OP_MODE::PROD);
        SelectFederationParams(TAPYRUS_OP_MODE::PROD);
    }
};

static int test_one_input_constructtransaction(const uint8_t* data, size_t size)
{
    static const ChainParamsSelection chain_params_selection;

    FuzzedDataProvider fdp(data, size);
    UniValue args;
    if (!args.read(fdp.ConsumeRemainingBytesAsString()) || !args.isArray()) return 0;

    try {
        // UniValue::operator[] returns a null UniValue past the end.
        ConstructTransaction(args[0], args[1], args[2], args[3]);
    } catch (const UniValue&) {
        // JSONRPCError(...) is thrown by value as a UniValue -- the
        // documented way malformed arguments are reported. Not a bug.
    } catch (const std::runtime_error&) {
        // UniValue's get_*() accessors throw std::runtime_error on a type
        // mismatch -- the same argument-validation path. Not a bug.
    }
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_constructtransaction(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_constructtransaction(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
