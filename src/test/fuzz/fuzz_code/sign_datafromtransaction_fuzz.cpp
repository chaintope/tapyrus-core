// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for DataFromTransaction (src/script/sign.cpp): extracts
// existing signature data out of tx.vin[nIn].scriptSig against txout,
// via VerifyScript()'s own extractor checker. Pure, no node context
// required. tx must have at least one input -- DataFromTransaction's own
// `assert(tx.vin.size() > nIn)` would otherwise abort, which is a
// harness-input-validity issue, not the kind of bug fuzzing this
// function is meant to find.

#include <tapyrus-config.h>

#include <primitives/transaction.h>
#include <script/sign.h>
#include <streams.h>
#include <version.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <exception>
#include <unistd.h>
#include <vector>

static int test_one_input_datafromtransaction(const uint8_t* data, size_t size)
{
    FuzzedDataProvider fdp(data, size);
    const std::vector<uint8_t> tx_bytes = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, fdp.remaining_bytes()));
    const std::vector<uint8_t> txout_bytes = fdp.ConsumeRemainingBytes<uint8_t>();

    CMutableTransaction mtx;
    {
        CDataStream ds(reinterpret_cast<const char*>(tx_bytes.data()),
                       reinterpret_cast<const char*>(tx_bytes.data() + tx_bytes.size()),
                       SER_NETWORK, PROTOCOL_VERSION);
        try {
            ds >> mtx;
        } catch (const std::exception&) {
            return 0;
        }
    }
    if (mtx.vin.empty()) return 0;

    CTxOut txout;
    {
        CDataStream ds(reinterpret_cast<const char*>(txout_bytes.data()),
                       reinterpret_cast<const char*>(txout_bytes.data() + txout_bytes.size()),
                       SER_NETWORK, PROTOCOL_VERSION);
        try {
            ds >> txout;
        } catch (const std::exception&) {
            return 0;
        }
    }

    DataFromTransaction(mtx, 0, txout);
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_datafromtransaction(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_datafromtransaction(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
