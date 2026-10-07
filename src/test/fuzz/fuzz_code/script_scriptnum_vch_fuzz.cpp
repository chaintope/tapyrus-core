// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for CScriptNum(const std::vector<unsigned char>&, bool,
// size_t) (src/script/script.h) -- the serialized-byte-vector constructor
// overload. Distinct from script_scriptnum_int64_fuzz.cpp, which targets
// the raw-integer overload; this one exercises the minimal-encoding and
// max-size validation that overload skips entirely, throwing
// scriptnum_error on a too-long or non-minimally-encoded vch.

#include <tapyrus-config.h>

#include <script/script.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <unistd.h>
#include <vector>

static int test_one_input_scriptnum_vch(const uint8_t* data, size_t size)
{
    FuzzedDataProvider fdp(data, size);
    const bool require_minimal = fdp.ConsumeBool();
    const std::vector<uint8_t> vch = fdp.ConsumeRemainingBytes<uint8_t>();

    try {
        CScriptNum num(vch, require_minimal);
    } catch (const scriptnum_error&) {
        // Documented: thrown on a too-long or non-minimally-encoded vch
        // when require_minimal is set. Not a bug.
    }
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_scriptnum_vch(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_scriptnum_vch(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
