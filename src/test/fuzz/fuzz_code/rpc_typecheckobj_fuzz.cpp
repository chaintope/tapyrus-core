// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for RPCTypeCheckObj (src/rpc/server.cpp), the type-validation
// helper every RPC handler runs its own JSON object argument through before
// use. typesExpected is fixed to a representative set of VNUM/VSTR/VBOOL/
// any-type entries rather than derived from the input -- the function's own
// logic (present/missing key, matching/mismatching type, fStrict's unknown-
// key check) is what's under test, not any particular RPC's own schema.

#include <tapyrus-config.h>

#include <rpc/protocol.h>
#include <rpc/server.h>
#include <univalue.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <map>
#include <string>
#include <unistd.h>

static int test_one_input_rpctypecheckobj(const uint8_t* data, size_t size)
{
    FuzzedDataProvider fdp(data, size);
    const bool allow_null = fdp.ConsumeBool();
    const bool strict = fdp.ConsumeBool();
    const std::string json = fdp.ConsumeRemainingBytesAsString();

    UniValue obj;
    if (!obj.read(json) || !obj.isObject()) return 0;

    const std::map<std::string, UniValueType> types_expected = {
        {"amount", UniValueType(UniValue::VNUM)},
        {"address", UniValueType(UniValue::VSTR)},
        {"verbose", UniValueType(UniValue::VBOOL)},
        {"data", UniValueType()},
    };

    try {
        RPCTypeCheckObj(obj, types_expected, allow_null, strict);
    } catch (const UniValue&) {
        // JSONRPCError(...) is thrown by value as a UniValue -- the
        // documented way this function reports a missing/mismatched/
        // unexpected key. Not a bug.
    }
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_rpctypecheckobj(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_rpctypecheckobj(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
