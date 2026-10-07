// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for CBlockPolicyEstimator::estimateSmartFee (src/policy/fees.cpp).
// A freshly constructed CBlockPolicyEstimator is safe to query directly --
// its constructor fully initializes buckets/stats with no external data
// required, so no crash is possible from empty history, only from the
// function's own confTarget-range handling and its calls into
// estimateCombinedFee/estimateConservativeFee.

#include <tapyrus-config.h>

#include <policy/fees.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <vector>

static int test_one_input_estimatesmartfee(const uint8_t* data, size_t size)
{
    FuzzedDataProvider fdp(data, size);

    const int conf_target = fdp.ConsumeIntegral<int>();
    const bool conservative = fdp.ConsumeBool();
    const bool pass_fee_calc = fdp.ConsumeBool();

    CBlockPolicyEstimator estimator;
    FeeCalculation fee_calc;
    estimator.estimateSmartFee(conf_target, pass_fee_calc ? &fee_calc : nullptr, conservative);

    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_estimatesmartfee(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_estimatesmartfee(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
