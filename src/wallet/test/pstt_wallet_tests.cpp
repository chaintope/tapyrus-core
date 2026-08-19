// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <core_io.h>
#include <pstt.h>
#include <rpc/server.h>
#include <script/sign.h>
#include <script/standard.h>
#include <txmempool.h>
#include <validation.h>
#include <wallet/rpcwallet.h>
#include <wallet/test/test_tapyrus_wallet.h>
#include <wallet/wallet.h>

#include <boost/test/unit_test.hpp>

// -----------------------------------------------------------------------
// RPC-dispatch-level tests for the wallet's PSTT RPCs (walletupdatepstt,
// walletsignpstt, walletprocesspstt) -- these go through the real
// registered actors (tableRPC), the same way pstt_tests.cpp's
// pstt_rpc_* cases exercise the wallet-independent PSTT RPCs, so argument
// parsing and JSON result shape are covered, not just FillPSTT/
// SignPSTTInput underneath them.
//
// GetWalletForJSONRPCRequest() resolves the request's wallet via the
// process-wide vpwallets list (AddWallet/GetWallets), not via the fixture's
// own `wallet` member directly, so it must be registered there for
// dispatch to find it. TestWalletSetup owns `wallet` as a unique_ptr and
// isn't itself RPC-dispatch-ready, so this derives from it (per house
// style: inherit and extend, don't edit the shared fixture) instead of
// duplicating its chain/wallet setup.
// -----------------------------------------------------------------------

struct PsttWalletTestingSetup : public TestWalletSetup
{
    // No-op deleter: sharedWallet is a second handle onto the object the
    // base class's `wallet` unique_ptr already owns and destroys.
    PsttWalletTestingSetup() : TestWalletSetup(), sharedWallet(wallet.get(), [](CWallet*) {})
    {
        AddWallet(sharedWallet);
        RegisterWalletRPCCommands(tableRPC);
    }

    ~PsttWalletTestingSetup()
    {
        RemoveWallet(sharedWallet);
    }

    std::shared_ptr<CWallet> sharedWallet;
};

BOOST_AUTO_TEST_SUITE(pstt_wallet_tests)

static UniValue CallPsttWalletRPC(const std::string& method, const UniValue& params)
{
    BOOST_REQUIRE(tableRPC[method]);
    JSONRPCRequest request;
    request.strMethod = method;
    request.params = params;
    request.fHelp = false;
    rpcfn_type fn = tableRPC[method]->actor;
    return (*fn)(request);
}

// Funds `setup`'s wallet with a spendable P2PKH coin (confirmed on chain,
// scanned into the wallet) by spending its first coinbase output. Returns
// the funding transaction so callers can build a PSTT input against it.
static CTransactionRef FundWalletWithP2PKHCoin(TestWalletSetup& setup, CAmount amount)
{
    CPubKey pubkey;
    BOOST_REQUIRE(setup.wallet->GetKeyFromPool(pubkey, false));

    const CTransactionRef& coinbaseTx = setup.m_coinbase_txns[0];
    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput input;
    input.previous_txid = coinbaseTx->GetHashMalFix();
    input.prev_out_index = 0;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    input.utxo = coinbaseTx;
    pstt.inputs.push_back(input);

    PSTTOutput output;
    output.amount = amount;
    output.script = GetScriptForDestination(pubkey.GetID());
    pstt.outputs.push_back(output);

    FlatSigningProvider provider;
    provider.keys[setup.coinbaseKey.GetPubKey().GetID()] = setup.coinbaseKey;
    SignatureData sigdata;
    BOOST_REQUIRE(SignPSTTInput(provider, pstt, 0, sigdata, SIGHASH_ALL, SignatureScheme::ECDSA) == PSTTSignResult::OK);
    pstt.inputs[0].FromSignatureData(sigdata);
    BOOST_REQUIRE(sigdata.complete);

    CTransactionRef tx = MakeTransactionRef(ExtractPSTT(pstt));
    BOOST_REQUIRE(setup.AddToWalletAndMempool(tx));
    BOOST_REQUIRE(setup.ProcessBlockAndScanForWalletTxns(tx));
    return tx;
}

// Builds an unsigned, single-input PSTT spending `fundingTx`'s only output
// to a fresh, wallet-unrelated destination -- no UTXO attached yet, exactly
// what a Creator/Constructor step would hand to the Updater.
static PartiallySignedTapyrusTransaction MakeUnsignedSpendPstt(const CTransactionRef& fundingTx)
{
    CKey destKey;
    destKey.MakeNewKey(true);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput input;
    input.previous_txid = fundingTx->GetHashMalFix();
    input.prev_out_index = 0;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    pstt.inputs.push_back(input);

    PSTTOutput output;
    output.amount = fundingTx->vout[0].nValue - CENT;
    output.script = GetScriptForDestination(destKey.GetPubKey().GetID());
    pstt.outputs.push_back(output);

    return pstt;
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_walletupdatepstt_fills_utxo, PsttWalletTestingSetup)
{
    CTransactionRef fundingTx = FundWalletWithP2PKHCoin(*this, 1 * COIN);
    PartiallySignedTapyrusTransaction pstt = MakeUnsignedSpendPstt(fundingTx);

    UniValue params(UniValue::VARR);
    params.push_back(EncodePSTT(pstt));
    UniValue result = CallPsttWalletRPC("walletupdatepstt", params);
    BOOST_REQUIRE(result.isObject());
    BOOST_CHECK(!result.find_value("complete").get_bool()); // Updater role only -- never signs

    PartiallySignedTapyrusTransaction updated;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(updated, result.find_value("pstt").get_str(), decodeError));
    BOOST_REQUIRE(updated.inputs[0].utxo);
    BOOST_CHECK(updated.inputs[0].utxo->GetHashMalFix() == fundingTx->GetHashMalFix());
    BOOST_CHECK(updated.inputs[0].final_script_sig.empty());
    BOOST_CHECK(updated.inputs[0].partial_sigs.empty());
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_walletsignpstt_completes_after_update, PsttWalletTestingSetup)
{
    CTransactionRef fundingTx = FundWalletWithP2PKHCoin(*this, 1 * COIN);
    PartiallySignedTapyrusTransaction pstt = MakeUnsignedSpendPstt(fundingTx);

    UniValue updateParams(UniValue::VARR);
    updateParams.push_back(EncodePSTT(pstt));
    UniValue updateResult = CallPsttWalletRPC("walletupdatepstt", updateParams);

    UniValue signParams(UniValue::VARR);
    signParams.push_back(updateResult.find_value("pstt").get_str());
    UniValue signResult = CallPsttWalletRPC("walletsignpstt", signParams);
    BOOST_REQUIRE(signResult.isObject());
    BOOST_CHECK(signResult.find_value("complete").get_bool());

    PartiallySignedTapyrusTransaction signedPstt;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(signedPstt, signResult.find_value("pstt").get_str(), decodeError));
    BOOST_REQUIRE(!signedPstt.inputs[0].final_script_sig.empty());

    CTransactionRef finalTx = MakeTransactionRef(ExtractPSTT(signedPstt));
    CTxMempoolAcceptanceOptions opt;
    opt.flags = MempoolAcceptanceFlags::BYPASSS_LIMITS;
    LOCK(cs_main);
    BOOST_CHECK(AcceptToMemoryPool(finalTx, opt));
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_walletprocesspstt_updates_and_signs_in_one_call, PsttWalletTestingSetup)
{
    // No prior walletupdatepstt call -- walletprocesspstt must perform both
    // the Updater and Signer roles itself in a single pass.
    CTransactionRef fundingTx = FundWalletWithP2PKHCoin(*this, 1 * COIN);
    PartiallySignedTapyrusTransaction pstt = MakeUnsignedSpendPstt(fundingTx);

    UniValue params(UniValue::VARR);
    params.push_back(EncodePSTT(pstt));
    UniValue result = CallPsttWalletRPC("walletprocesspstt", params);
    BOOST_REQUIRE(result.isObject());
    BOOST_CHECK(result.find_value("complete").get_bool());

    PartiallySignedTapyrusTransaction processed;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(processed, result.find_value("pstt").get_str(), decodeError));
    BOOST_CHECK(!processed.inputs[0].final_script_sig.empty());
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_walletprocesspstt_sign_false_only_updates, PsttWalletTestingSetup)
{
    // sign=false must behave like walletupdatepstt: fill in the UTXO, but
    // leave the input unsigned.
    CTransactionRef fundingTx = FundWalletWithP2PKHCoin(*this, 1 * COIN);
    PartiallySignedTapyrusTransaction pstt = MakeUnsignedSpendPstt(fundingTx);

    UniValue params(UniValue::VARR);
    params.push_back(EncodePSTT(pstt));
    params.push_back(UniValue(false)); // sign=false
    UniValue result = CallPsttWalletRPC("walletprocesspstt", params);
    BOOST_REQUIRE(result.isObject());
    BOOST_CHECK(!result.find_value("complete").get_bool());

    PartiallySignedTapyrusTransaction processed;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(processed, result.find_value("pstt").get_str(), decodeError));
    BOOST_REQUIRE(processed.inputs[0].utxo);
    BOOST_CHECK(processed.inputs[0].final_script_sig.empty());
    BOOST_CHECK(processed.inputs[0].partial_sigs.empty());
}

BOOST_AUTO_TEST_SUITE_END()
