// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <consensus/validation.h>
#include <policy/packages.h>
#include <rpc/mempool.h>
#include <rpc/server.h>
#include <rpc/client.h>
#include <txmempool.h>
#include <utilstrencodings.h>

#include <stdint.h>

#include <univalue.h>

static void encodePackageResult(bool success, const PackageValidationState& pkg_results, CValidationState state, UniValue& result)
{
    UniValue result_0(UniValue::VOBJ);
    if(success && ArePackageTransactionsAccepted(pkg_results)) {
        for (const auto& r : pkg_results) {
            result_0.pushKV("allowed", true);
            result.pushKV(r.first.GetHex(), result_0);
        }
    }
    else if(state.IsInvalid() || state.IsError()) {
        result.pushKV("allowed", false);
        result.pushKV("reject-reason", strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
    } else {
        for (const auto& r : pkg_results) {
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
            "\nReturns if raw transaction (serialized, hex-encoded) would be accepted by mempool.\n"
            "\nThis checks if the transaction violates the consensus or policy rules.\n"
            "\nSee sendrawtransaction call.\n"
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
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nTest acceptance of the transaction (signed hex)\n"
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
                            "Too many transactions in package." );
    }

    Package package;
    for (const auto& rawtx : raw_transactions.getValues()) {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, rawtx.get_str())) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR,
                                "TX decode failed: " + rawtx.get_str() + " Make sure the tx has at least one input.");
        }
        package.push_back(MakeTransactionRef(std::move(mtx)));
    }

    if(package.size() != raw_transactions.size()){
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                    "Duplicate transaction found in package.");
    }

    CAmount max_raw_tx_fee = ::maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        max_raw_tx_fee = 0;
    }

    CValidationState state;
    PackageValidationState pkg_results;
    std::vector<const CTxMemPoolEntry> submitPool;
    submitPool.reserve(package.size());

    bool success = false;
    success = TestPackageAcceptance(package, state, pkg_results, &submitPool);

    UniValue result(UniValue::VOBJ);

    encodePackageResult(success, pkg_results, state, result);

    return result;
}

static UniValue submitpackage(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
    throw std::runtime_error(
        "submitpackage \"[rawtx1, rawtx2]\" \n"
        "\nSubmit a package of raw transactions (serialized, hex-encoded) to local node \n"
        "\nArguments:\n"
        "1. transaction_hex                        (array, required) An array of hex strings of raw transactions.\n"
        "2. allowhighfees                            (boolean, optional, default=false) Allow high fees\n"
        "\nResult:\n"
        "   {\n"
        "    \"allowed\"               (boolean) If submitting all transaction was successful \n"
        "   }\n"
        "\nExamples:\n"
        "\nCreate a transaction\n"
        + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
        "Sign the transaction, and get back the hex\n"
        + HelpExampleCli("signrawtransaction", "\"myhex\"") +
        "\nSubmit the transaction as a package (signed hex)\n"
        + HelpExampleCli("submitpackage", "\"raw_tx1, raw_tx2\"") +
        "\nAs a json rpc call\n"
        + HelpExampleRpc("submitpackage", "[\"raw_tx1, raw_tx2\"]")
    );

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VBOOL});

    const UniValue raw_transactions = request.params[0].get_array();
    if (raw_transactions.size() < 1 || raw_transactions.size() > MAX_PACKAGE_COUNT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                            "Too many transactions in package");
    }

    Package package;
    for (const auto& rawtx : raw_transactions.getValues()) {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, rawtx.get_str())) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR,
                                "TX decode failed: " + rawtx.get_str() + " Make sure the tx has at least one input.");
        }
        package.push_back(MakeTransactionRef(std::move(mtx)));
    }

    if(package.size() != raw_transactions.size()){
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                    "Duplicate transaction found in package.");
    }

    CAmount max_raw_tx_fee = ::maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool()) {
        max_raw_tx_fee = 0;
    }

    CValidationState state;
    PackageValidationState pkg_results;
    std::vector<const CTxMemPoolEntry> submitPool;
    submitPool.reserve(package.size());

    bool success = TestPackageAcceptance(package, state, pkg_results, &submitPool);

    UniValue result(UniValue::VOBJ);

    if(success && ArePackageTransactionsAccepted(pkg_results))
    {
        success = SubmitPackageToMempool(submitPool, state);
    }
    encodePackageResult(success, pkg_results, state, result);

    return result;
}


static const CRPCCommand commands[] =
{ //  category              name                            actor (function)            argNames
  //  --------------------- ------------------------        -----------------------     ----------
    { "packages",        "testmempoolaccept",           &testmempoolaccept,         {"rawtxs","allowhighfees"} },
    { "packages",           "submitpackage",                    &submitpackage,             {"rawtxs","allowhighfees"} },
};

void RegisterMempoolRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
