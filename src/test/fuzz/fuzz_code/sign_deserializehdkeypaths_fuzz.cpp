// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for DeserializeHDKeypaths<Stream> (src/script/sign.h),
// instantiated with CDataStream -- the same stream type PSBT/PSTT-style
// deserialization uses throughout this codebase. Pure, no node context
// required.

#include <tapyrus-config.h>

#include <pubkey.h>
#include <script/sign.h>
#include <streams.h>
#include <version.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <ios>
#include <map>
#include <unistd.h>
#include <vector>

static int test_one_input_deserializehdkeypaths(const uint8_t* data, size_t size)
{
    FuzzedDataProvider fdp(data, size);
    const std::vector<uint8_t> key = fdp.ConsumeBytes<uint8_t>(fdp.ConsumeIntegralInRange<size_t>(0, 128));
    const std::vector<uint8_t> stream_bytes = fdp.ConsumeRemainingBytes<uint8_t>();

    CDataStream ds(reinterpret_cast<const char*>(stream_bytes.data()),
                   reinterpret_cast<const char*>(stream_bytes.data() + stream_bytes.size()),
                   SER_NETWORK, PROTOCOL_VERSION);

    std::map<CPubKey, std::vector<uint32_t>> hd_keypaths;
    try {
        DeserializeHDKeypaths(ds, key, hd_keypaths);
    } catch (const std::ios_base::failure&) {
        // Documented: thrown on a malformed key size, invalid pubkey,
        // duplicate key, or malformed keypath length. Not a bug.
    }
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_deserializehdkeypaths(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_deserializehdkeypaths(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
