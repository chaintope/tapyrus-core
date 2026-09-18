// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for the real decodepstt RPC handler (src/rpc/rawtransaction.cpp),
// as opposed to pstt_decode_fuzz.cpp's direct call to DecodePSTT alone.
// decodepstt is the one call site that reaches EstimatePsttFee and
// EstimateInputScriptSig (both `static` in rawtransaction.cpp, so not
// independently linkable from a separate fuzz executable) -- both handle
// RPC-client-controlled PSTT contents directly: EstimatePsttFee reads a
// boost::optional<CAmount> that isn't always set, EstimateInputScriptSig
// indexes input.utxo->vout.at(prevout_index) against an
// attacker-controlled index. decodepstt also runs
// the dry-run SignPSTTInput-against-DUMMY_SIGNING_PROVIDER "next"-role
// computation pstt_signer_fuzz.cpp fuzzes directly, so this target's
// real value is the two Estimate* functions and decodepstt's own
// UniValue-assembly code, not re-covering SignPSTTInput itself.
//
// decodepstt has no chain/wallet dependency (confirmed by reading its
// full body: no pwallet/ChainActive/GetTransaction/mempool use), so it's
// callable here with only a bare JSONRPCRequest wrapping the fuzzed
// string as request.params[0] -- exactly the one argument real RPC
// clients send.

#include <tapyrus-config.h>

#include <core_io.h>
#include <key.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <univalue.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

extern UniValue decodepstt(const JSONRPCRequest& request);

static int test_one_input(const uint8_t* data, size_t size)
{
    const std::string base64_pstt(reinterpret_cast<const char*>(data), size);

    JSONRPCRequest request;
    request.params = UniValue(UniValue::VARR);
    request.params.push_back(base64_pstt);

    try {
        decodepstt(request);
    } catch (const UniValue&) {
        // JSONRPCError(...) is thrown by value as a UniValue -- the
        // documented way decodepstt reports a decode/sanity failure
        // (see src/rpc/rawtransaction.cpp). Not a bug.
    } catch (const std::exception&) {
        // UniValue's own get_str()/type checks throw std::runtime_error
        // on an unexpected shape; not reachable here since params[0] is
        // always a string, but kept for the same reason
        // pstt_parse_fuzz.cpp keeps it.
    }
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
