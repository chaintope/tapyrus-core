// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for IsStandard (src/policy/policy.cpp): classifies a
// scriptPubKey via Solver() and applies this project's own standardness
// rules on top (multisig m/n bounds, datacarrier size limit). Pure, no
// setup required -- raw fuzz bytes ARE a valid CScript (any byte string
// parses as one, just not necessarily a meaningful one).

#include <tapyrus-config.h>

#include <policy/policy.h>
#include <script/script.h>
#include <script/standard.h>

#include <cstdint>
#include <unistd.h>
#include <vector>

static int test_one_input_isstandard(const uint8_t* data, size_t size)
{
    const CScript script_pub_key(data, data + size);
    txnouttype which_type;
    IsStandard(script_pub_key, which_type);
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_isstandard(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_isstandard(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
