// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for CBlockPolicyEstimator::Read (src/policy/fees.cpp): parses
// a fee_estimates.dat file from disk, including the three
// TxConfirmStats::Read calls that deserialize each bucket history.
// TxConfirmStats is private to fees.cpp, so this is its only entry point.
// The fuzz input is handed over as the file's contents via fmemopen().
// No node context required.

#include <tapyrus-config.h>

#include <clientversion.h>
#include <policy/fees.h>
#include <streams.h>

#include <cstdint>
#include <cstdio>
#include <unistd.h>

static int test_one_input_feeestimator_read(const uint8_t* data, size_t size)
{
    // fmemopen() rejects a zero-length buffer.
    if (size == 0) return 0;

    // "rb": the buffer is only ever read, so casting away const is safe.
    FILE* file = fmemopen(const_cast<uint8_t*>(data), size, "rb");
    if (file == nullptr) return 0;

    // CAutoFile takes ownership and fclose()s the stream.
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    CBlockPolicyEstimator estimator;
    // Read() reports a corrupt file by returning false (it catches its own
    // std::exceptions) -- not a bug.
    estimator.Read(filein);
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_feeestimator_read(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_feeestimator_read(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
