// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for IsStandardTx (src/policy/policy.cpp): applies this
// project's standardness rules (feature range, size limit, scriptSig
// push-only, per-output IsStandard()) to a deserialized transaction.
// Pure, no node context required.

#include <tapyrus-config.h>

#include <policy/policy.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <version.h>

#include <cstdint>
#include <exception>
#include <string>
#include <unistd.h>

static int test_one_input_isstandardtx(const uint8_t* data, size_t size)
{
    CDataStream ds(reinterpret_cast<const char*>(data),
                   reinterpret_cast<const char*>(data) + size,
                   SER_NETWORK, PROTOCOL_VERSION);

    CMutableTransaction mtx;
    try {
        ds >> mtx;
    } catch (const std::exception&) {
        return 0;
    }

    const CTransaction tx(mtx);
    std::string reason;
    IsStandardTx(tx, reason);
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_isstandardtx(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_isstandardtx(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
