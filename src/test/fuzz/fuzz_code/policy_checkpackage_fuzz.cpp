// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for CheckPackage (src/policy/packages.cpp): the context-free
// package-relay checks (transaction count, total size, dependency order,
// conflicting inputs) run on a package before any of it touches the
// mempool. The input is a network-serialized vector of transactions.
// No node context required.

#include <tapyrus-config.h>

#include <consensus/validation.h>
#include <policy/packages.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <version.h>

#include <cstdint>
#include <exception>
#include <utility>
#include <vector>
#include <unistd.h>

static int test_one_input_checkpackage(const uint8_t* data, size_t size)
{
    CDataStream ds(reinterpret_cast<const char*>(data),
                   reinterpret_cast<const char*>(data) + size,
                   SER_NETWORK, PROTOCOL_VERSION);

    std::vector<CMutableTransaction> mtxs;
    try {
        ds >> mtxs;
    } catch (const std::exception&) {
        return 0;
    }

    Package package;
    package.reserve(mtxs.size());
    for (CMutableTransaction& mtx : mtxs) {
        package.push_back(MakeTransactionRef(std::move(mtx)));
    }

    CValidationState state;
    CheckPackage(package, state);
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_checkpackage(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_checkpackage(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
