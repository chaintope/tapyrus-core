// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for CScriptNum(const int64_t&) (src/script/script.h) --
// the raw-integer constructor overload. Distinct from
// script_scriptnum_vch_fuzz.cpp, which targets the other overload
// (constructing from a serialized byte vector, with its own minimal-
// encoding/size validation this overload skips entirely).

#include <tapyrus-config.h>

#include <script/script.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <unistd.h>

static int test_one_input_scriptnum_int64(const uint8_t* data, size_t size)
{
    FuzzedDataProvider fdp(data, size);
    const int64_t n = fdp.ConsumeIntegral<int64_t>();
    CScriptNum num(n);
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_scriptnum_int64(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_scriptnum_int64(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
