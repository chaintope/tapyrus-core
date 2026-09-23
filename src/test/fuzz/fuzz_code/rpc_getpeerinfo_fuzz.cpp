// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for the "getpeerinfo" RPC (src/rpc/net.cpp), dispatched
// through the real tableRPC.execute() against the shared FuzzNodeSetup
// node context (see fuzz_node_setup.h) -- exactly how a real RPC
// client's request reaches it. getpeerinfo itself is a static function
// in its own translation unit; dispatch-by-name through tableRPC is what
// makes calling it from here possible at all.

#include <tapyrus-config.h>

#include <rpc/protocol.h>
#include <rpc/server.h>
#include <univalue.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>
#include <test/fuzz/fuzz_code/fuzz_node_setup.h>

#include <cstdint>
#include <string>
#include <unistd.h>

static int test_one_input_getpeerinfo(const uint8_t* data, size_t size)
{
    GetFuzzNodeSetup();

    FuzzedDataProvider fdp(data, size);
    const std::string json = fdp.ConsumeRemainingBytesAsString();

    UniValue params;
    if (!params.read(json)) return 0;

    JSONRPCRequest request;
    request.strMethod = "getpeerinfo";
    request.params = params;

    try {
        tableRPC.execute(request);
    } catch (const UniValue&) {
        // JSONRPCError(...) is thrown by value as a UniValue -- the
        // documented way an RPC handler reports a malformed-argument or
        // application-level error. Not a bug.
    }
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_getpeerinfo(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_getpeerinfo(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
