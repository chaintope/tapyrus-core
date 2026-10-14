// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <consensus/validation.h>
#include <policy/packages.h>
#include <policy/policy.h>
#include <rpc/mempool.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <txmempool.h>
#include <util.h>
#include <utilstrencodings.h>
#include <validation.h>
#include <file_io.h>

#include <stdexcept>
#include <stdint.h>

#include <univalue.h>
#include <logging.h>

static void encodePackageResult(bool success, const PackageValidationState& pkg_results, CValidationState state, UniValue& result)
{
    if(success && ArePackageTransactionsAccepted(pkg_results)) {
        for (const auto& r : pkg_results) {
            UniValue result_0(UniValue::VOBJ);
            result_0.pushKV("allowed", true);
            result.pushKV(r.first.GetHex(), result_0);
        }
    }
    else if(state.IsInvalid() || state.IsError()) {
        result.pushKV("allowed", false);
        result.pushKV("reject-reason", strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
    } else {
        for (const auto& r : pkg_results) {
            UniValue result_0(UniValue::VOBJ);
            if(r.second.IsInvalid() || r.second.IsError()) {
                result_0.pushKV("allowed", false);
                result_0.pushKV("reject-reason", strprintf("%i: %s", r.second.GetRejectCode(), r.second.GetRejectReason()));
            } else if (r.second.missingInputs && r.second.IsValid()) {
                result_0.pushKV("allowed", false);
                result_0.pushKV("reject-reason", "missing-inputs");
            } else
                result_0.pushKV("allowed", true);
            result.pushKV(r.first.GetHex(), result_0);
        }
    }
}

static UniValue testmempoolaccept(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            // clang-format off
            "testmempoolaccept [\"rawtxs\"] ( allowhighfees )\n"
            "\nTest whether raw transactions would be accepted by the mempool without submitting them.\n"
            "\nThis is a dry-run: transactions are validated against mempool policy but never inserted\n"
            "\nor relayed to peers. Use submitpackage or sendrawtransaction to actually submit.\n"
            "\nThis RPC does not guarantee the submission of all transactions in the package."
            "\nOnly valid transactions are successful. Other transactions that violate\n"
            "\nthe consensus or policy rules are rejected.\n"
            "\nArguments:\n"
            "1. [\"rawtxs\"]        (array, required) An array of hex strings of raw transactions.\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "  \"allowed\"            (boolean) If the mempool allows this tx to be inserted\n"
            "  \"reject-reason\"  (string) Rejection string (only present when 'allowed' is false)\n"
            "[                   (array) The result of the mempool acceptance test for each raw transaction in the input array. Length is equal to the number of input transactions."
            " {\n"
            "  \"<txid>:\"                  (string) The transaction id in hex\n"
            "   {\n"
            "    \"allowed\"               (boolean) If the mempool allows this tx to be inserted\n"
            "    \"reject-reason\"     (string) Rejection string (only present when 'allowed' is false)\n"
            "   }\n"
            " }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("testmempoolaccept", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("testmempoolaccept", "[\"signedhex\"]")
            // clang-format on
            );
    }

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VBOOL});
    const UniValue raw_transactions = request.params[0].get_array();
    if (raw_transactions.size() < 1 || raw_transactions.size() > MAX_PACKAGE_COUNT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                            strprintf("Too many transactions in package (maximum %u).", MAX_PACKAGE_COUNT));
    }

    std::vector<CTransaction> transactions;
    for (const auto& rawtx : raw_transactions.getValues()) {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, rawtx.get_str())) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR,
                                "TX decode failed: " + rawtx.get_str() + " Make sure the tx has at least one input.");
        }
        transactions.push_back(std::move(mtx));
    }

    CAmount max_raw_tx_fee = ::maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        max_raw_tx_fee = 0;
    }

    //make a package with only unknown transactions i.e. omit transactions which are already in the mempool
    Package package;
    CValidationState state;
    PackageValidationState pkg_results;

    FilterMempoolDuplicates(transactions, package, pkg_results);

    bool success = false;
    CTxMempoolAcceptanceOptions opt;
    opt.context = ValidationContext::PACKAGE;
    opt.nAbsurdFee = max_raw_tx_fee;
    opt.flags = MempoolAcceptanceFlags::TEST_ONLY;

    success = SubmitPackageToMempool(package, state, pkg_results, opt);

    UniValue result(UniValue::VOBJ);

    encodePackageResult(success, pkg_results, state, result);

    return result;
}

static UniValue submitpackage(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
    throw std::runtime_error(
        "submitpackage \"[rawtx1, rawtx2]\" \n"
        "\nSubmit a package of raw transactions (serialized, hex-encoded) to local node.\n"
        "\nTransactions are submitted to the mempool individually, in package order.\n"
        "\nPackage admission is NOT atomic: if an earlier transaction succeeds but a later\n"
        "\none fails, the admitted prefix remains in the mempool. Check per-transaction\n"
        "\nresults to determine which transactions were admitted.\n"
        "\nOnly valid transactions are successful. Transactions that violate consensus\n"
        "\nor policy rules are rejected.\n"
        "\nA valid package submission may fail or be evicted when the memory pool is full.\n"
        "\nArguments:\n"
        "1. transaction_hex                        (array, required) An array of hex strings of raw transactions.\n"
        "2. allowhighfees                            (boolean, optional, default=false) Allow high fees\n"
        "\nResult:\n"
        "   {\n"
        "    \"allowed\"               (boolean) If submitting all transaction was successful \n"
        "   }\n"
        "\nExamples:\n"
        + HelpExampleCli("submitpackage", "\"raw_tx1, raw_tx2\"") +
        "\nAs a json rpc call\n"
        + HelpExampleRpc("submitpackage", "[\"raw_tx1, raw_tx2\"]")
    );

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VBOOL});

    const UniValue raw_transactions = request.params[0].get_array();
    if (raw_transactions.size() < 1 || raw_transactions.size() > MAX_PACKAGE_COUNT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                            strprintf("Too many transactions in package (maximum %u)", MAX_PACKAGE_COUNT));
    }

    std::vector<CTransaction> transactions;
    for (const auto& rawtx : raw_transactions.getValues()) {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, rawtx.get_str())) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR,
                                "TX decode failed: " + rawtx.get_str() + " Make sure the tx has at least one input.");
        }
        transactions.push_back(std::move(mtx));
    }

    CAmount max_raw_tx_fee = ::maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        max_raw_tx_fee = 0;
    }

    //make a package with only unknown transactions i.e. omit transactions which are already in the mempool
    Package package;
    CValidationState state;
    PackageValidationState pkg_results;

    FilterMempoolDuplicates(transactions, package, pkg_results);

    CTxMempoolAcceptanceOptions opt;
    opt.context = ValidationContext::PACKAGE;
    opt.nAbsurdFee = max_raw_tx_fee;

    bool success = SubmitPackageToMempool(package, state, pkg_results, opt);

    UniValue result(UniValue::VOBJ);

    encodePackageResult(success, pkg_results, state, result);

    return result;
}


static std::string EntryDescriptionString()
{
    return "    \"size\" : n,             (numeric) transaction size in bytes. \n"
           "    \"fee\" : n,              (numeric) transaction fee in " + CURRENCY_UNIT + " (DEPRECATED)\n"
           "    \"modifiedfee\" : n,      (numeric) transaction fee with fee deltas used for mining priority (DEPRECATED)\n"
           "    \"time\" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,           (numeric) block height when transaction entered pool\n"
           "    \"descendantcount\" : n,  (numeric) number of in-mempool descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,   (numeric) transaction size of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,   (numeric) modified fees (see above) of in-mempool descendants (including this one) (DEPRECATED)\n"
           "    \"ancestorcount\" : n,    (numeric) number of in-mempool ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,     (numeric) transaction size of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,     (numeric) modified fees (see above) of in-mempool ancestors (including this one) (DEPRECATED)\n"
           "    \"fees\" : {\n"
           "        \"base\" : n,         (numeric) transaction fee in " + CURRENCY_UNIT + "\n"
           "        \"modified\" : n,     (numeric) transaction fee with fee deltas used for mining priority in " + CURRENCY_UNIT + "\n"
           "        \"ancestor\" : n,     (numeric) modified fees (see above) of in-mempool ancestors (including this one) in " + CURRENCY_UNIT + "\n"
           "        \"descendant\" : n,   (numeric) modified fees (see above) of in-mempool descendants (including this one) in " + CURRENCY_UNIT + "\n"
           "    }\n"
           "    \"depends\" : [           (array) unconfirmed transactions used as inputs for this transaction\n"
           "        \"transactionid\",    (string) parent transaction id\n"
           "       ... ]\n"
           "    \"spentby\" : [           (array) unconfirmed transactions spending outputs from this transaction\n"
           "        \"transactionid\",    (string) child transaction id\n"
           "       ... ]\n";
}

static void entryToJSON(UniValue &info, const CTxMemPoolEntry &e) EXCLUSIVE_LOCKS_REQUIRED(::mempool.cs)
{
    AssertLockHeld(mempool.cs);

    UniValue fees(UniValue::VOBJ);
    fees.pushKV("base", ValueFromAmount(e.GetFee()));
    fees.pushKV("modified", ValueFromAmount(e.GetModifiedFee()));
    fees.pushKV("ancestor", ValueFromAmount(e.GetModFeesWithAncestors()));
    fees.pushKV("descendant", ValueFromAmount(e.GetModFeesWithDescendants()));
    info.pushKV("fees", fees);

    info.pushKV("size", (int)e.GetTxSize());
    info.pushKV("fee", ValueFromAmount(e.GetFee()));
    info.pushKV("modifiedfee", ValueFromAmount(e.GetModifiedFee()));
    info.pushKV("time", e.GetTime());
    info.pushKV("height", (int)e.GetHeight());
    info.pushKV("descendantcount", e.GetCountWithDescendants());
    info.pushKV("descendantsize", e.GetSizeWithDescendants());
    info.pushKV("descendantfees", e.GetModFeesWithDescendants());
    info.pushKV("ancestorcount", e.GetCountWithAncestors());
    info.pushKV("ancestorsize", e.GetSizeWithAncestors());
    info.pushKV("ancestorfees", e.GetModFeesWithAncestors());
    info.pushKV("txid", mempool.vTxHashes[e.vTxHashesIdx].first.ToString());
    const CTransaction& tx = e.GetTx();
    std::set<std::string> setDepends;
    for (const CTxIn& txin : tx.vin)
    {
        if (mempool.exists(txin.prevout.hashMalFix))
            setDepends.insert(txin.prevout.hashMalFix.ToString());
    }

    UniValue depends(UniValue::VARR);
    for (const std::string& dep : setDepends)
    {
        depends.push_back(dep);
    }

    info.pushKV("depends", depends);

    UniValue spent(UniValue::VARR);
    const CTxMemPool::txiter &it = mempool.mapTx.find(tx.GetHashMalFix());
    const CTxMemPool::setEntries &setChildren = mempool.GetMemPoolChildren(it);
    for (const CTxMemPool::txiter &childiter : setChildren) {
        spent.push_back(childiter->GetTx().GetHashMalFix().ToString());
    }

    info.pushKV("spentby", spent);
}

UniValue mempoolToJSON(bool fVerbose)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        for (const CTxMemPoolEntry& e : mempool.mapTx)
        {
            const uint256& hash = e.GetTx().GetHashMalFix();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.pushKV(hash.ToString(), info);
        }
        return o;
    }
    else
    {
        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        for (const uint256& hash : vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

static UniValue getrawmempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nHint: use getmempoolentry to fetch a specific transaction from the mempool.\n"
            "\nArguments:\n"
            "1. verbose (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    bool fVerbose = false;
    if (!request.params[0].isNull())
        fVerbose = request.params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

static UniValue getmempoolancestors(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempoolancestors txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool ancestors.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool ancestor transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolancestors", "\"mytxid\"")
            + HelpExampleRpc("getmempoolancestors", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setAncestors;
    uint64_t noLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    mempool.CalculateMemPoolAncestors(*it, setAncestors, noLimit, noLimit, noLimit, noLimit, dummy, false);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            o.push_back(ancestorIt->GetTx().GetHashMalFix().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter ancestorIt : setAncestors) {
            const CTxMemPoolEntry &e = *ancestorIt;
            const uint256& _hash = e.GetTx().GetHashMalFix();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.pushKV(_hash.ToString(), info);
        }
        return o;
    }
}

static UniValue getmempooldescendants(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempooldescendants txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool descendants.\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool descendant transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempooldescendants", "\"mytxid\"")
            + HelpExampleRpc("getmempooldescendants", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (!request.params[1].isNull())
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setDescendants;
    mempool.CalculateDescendants(it, setDescendants);
    // CTxMemPool::CalculateDescendants will include the given tx
    setDescendants.erase(it);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            o.push_back(descendantIt->GetTx().GetHashMalFix().ToString());
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for (CTxMemPool::txiter descendantIt : setDescendants) {
            const CTxMemPoolEntry &e = *descendantIt;
            const uint256& _hash = e.GetTx().GetHashMalFix();
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.pushKV(_hash.ToString(), info);
        }
        return o;
    }
}

static UniValue getmempoolentry(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getmempoolentry txid\n"
            "\nReturns mempool data for given transaction\n"
            "\nArguments:\n"
            "1. \"txid\"                   (string, required) The transaction id (must be in mempool)\n"
            "\nResult:\n"
            "{                           (json object)\n"
            + EntryDescriptionString()
            + "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolentry", "\"mytxid\"")
            + HelpExampleRpc("getmempoolentry", "\"mytxid\"")
        );
    }

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    const CTxMemPoolEntry &e = *it;
    UniValue info(UniValue::VOBJ);
    entryToJSON(info, e);
    return info;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("size", (int64_t) mempool.size());
    ret.pushKV("bytes", (int64_t) mempool.GetTotalTxSize());
    ret.pushKV("usage", (int64_t) mempool.DynamicMemoryUsage());
    size_t maxmempool = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.pushKV("maxmempool", (int64_t) maxmempool);
    ret.pushKV("mempoolminfee", ValueFromAmount(std::max(mempool.GetMinFee(maxmempool), ::minRelayTxFee).GetFeePerK()));
    ret.pushKV("minrelaytxfee", ValueFromAmount(::minRelayTxFee.GetFeePerK()));

    return ret;
}

static UniValue getmempoolinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "  \"bytes\": xxxxx,              (numeric) Sum of all transaction sizes.\n"
            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee rate in " + CURRENCY_UNIT + "/kB for tx to be accepted. Is the maximum of minrelaytxfee and minimum mempool fee\n"
            "  \"minrelaytxfee\": xxxxx       (numeric) Current minimum relay fee for transactions\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

static UniValue savemempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "savemempool\n"
            "\nDumps the mempool to disk. It will fail until the previous dump is fully loaded.\n"
            "\nExamples:\n"
            + HelpExampleCli("savemempool", "")
            + HelpExampleRpc("savemempool", "")
        );
    }

    if (!g_is_mempool_loaded) {
        throw JSONRPCError(RPC_MISC_ERROR, "The mempool was not loaded yet");
    }

    if (!DumpMempool()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unable to dump mempool to disk");
    }

    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category              name                            actor (function)            argNames
  //  --------------------- ------------------------        -----------------------     ----------
    { "blockchain",         "getmempoolancestors",    &getmempoolancestors,    {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  &getmempooldescendants,  {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        &getmempoolentry,        {"txid"} },
    { "blockchain",         "getmempoolinfo",         &getmempoolinfo,         {} },
    { "blockchain",         "getrawmempool",          &getrawmempool,          {"verbose"} },
    { "blockchain",         "savemempool",            &savemempool,            {} },

    { "packages",        "testmempoolaccept",           &testmempoolaccept,         {"rawtxs","allowhighfees"} },
    { "packages",           "submitpackage",                    &submitpackage,             {"rawtxs","allowhighfees"} },
};

void RegisterMempoolRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
