// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz-only placeholder definitions -- see add_pstt_fuzz_target_with_rpc_glue()
// in src/test/CMakeLists.txt for why these exist. ParsePsttInputEntries/
// decodepstt (the only functions these fuzz harnesses actually call) live in
// rpc/rawtransaction.cpp next to sendrawtransaction/gettxoutproof/
// createrawtransaction/signrawtransaction and friends; a static archive is
// pulled in per translation unit, so the whole file comes along once either
// function is referenced. Tracing the undefined-symbol chain (see the
// comment in src/test/CMakeLists.txt) showed exactly one chokepoint keeping
// that dead code from needing the entire node: rawtransaction.cpp's
// deprecated signrawtransaction alias references three functions defined in
// wallet/rpcwallet.cpp, and pulling that one translation unit in turn drags
// in CWallet's entire API, mining.cpp (for its own generate handler) and
// httpserver.cpp (for URL decoding) -- none of it reachable from
// ParsePsttInputEntries/decodepstt, none of it ever executed by these
// harnesses. Defining the three functions here breaks that chain without
// needing tapyrus_wallet, tapyrus_zmq, or their init-time glue at all.
//
// g_connman is different: its storage normally lives in init.cpp (daemon
// glue, never compiled into a library), needed here only because
// sendrawtransaction (another never-called sibling) references it directly.
// CConnman's real destructor (tapyrus_peer, linked for real by this fuzz
// target for other reasons -- see the CMakeLists.txt comment) handles it
// correctly; this just supplies the missing global.

#include <net.h>
#include <univalue.h>
#include <wallet/rpcwallet.h>

#include <stdexcept>

std::shared_ptr<CWallet> GetWalletForJSONRPCRequest(const JSONRPCRequest&) { return nullptr; }
std::string HelpRequiringPassphrase(CWallet*) { return std::string(); }
UniValue signrawtransactionwithwallet(const JSONRPCRequest&)
{
    throw std::runtime_error("unreachable: fuzz-only stub, never invoked by the harness");
}

std::unique_ptr<CConnman> g_connman;
