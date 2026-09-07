// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <core_io.h>
#include <key_io.h>
#include <pstt.h>
#include <rpc/client.h>
#include <rpc/server.h>
#include <script/sign.h>
#include <script/standard.h>
#include <txmempool.h>
#include <validation.h>
#include <wallet/pstt.h>
#include <wallet/rpcwallet.h>
#include <wallet/test/test_tapyrus_wallet.h>
#include <wallet/test/wallet_test_fixture.h>
#include <wallet/wallet.h>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

extern bool ParseHDKeypath(std::string keypath_str, std::vector<uint32_t>& keypath);

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

BOOST_FIXTURE_TEST_SUITE(pstt_wallet_tests, WalletTestingSetup)

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

// Mirrors rpc_tests.cpp's CallRPC: space-split CLI-style string arguments,
// run through RPCConvertValues exactly as tapyrus-cli does, rather than
// pre-typed UniValue params. Used to guard src/rpc/client.cpp's
// vRPCConvertParams table -- without a conversion entry for a given
// array/object/numeric/boolean argument, tapyrus-cli's string arguments
// pass through unconverted and RPCTypeCheck throws a generic type error
// before ever reaching real argument validation. CallPsttWalletRPC above
// (pre-typed UniValue params) can't catch that class of bug. Also mirrors
// CallRPC's UniValue->std::runtime_error translation: without it, an error
// raised by the RPC body itself (rather than by argument parsing) would
// escape as a UniValue and fail the calling BOOST_CHECK_THROW(...,
// std::runtime_error) case as an unrecognized exception instead of cleanly.
static UniValue CallPsttWalletRPCFromCli(const std::string& args)
{
    std::vector<std::string> vArgs;
    boost::split(vArgs, args, boost::is_any_of(" \t"));
    std::string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    BOOST_REQUIRE(tableRPC[strMethod]);
    JSONRPCRequest request;
    request.strMethod = strMethod;
    request.params = RPCConvertValues(strMethod, vArgs);
    request.fHelp = false;
    rpcfn_type fn = tableRPC[strMethod]->actor;
    try {
        return (*fn)(request);
    }
    catch (const UniValue& objError) {
        throw std::runtime_error(objError.find_value("message").get_str());
    }
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
    input.prevout_index = 0;
    input.previous_txid_set = true;
    input.prevout_index_set = true;
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
    input.prevout_index = 0;
    input.previous_txid_set = true;
    input.prevout_index_set = true;
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

// Wallet-RPC counterpart to rpc_tests.cpp's rpc_pstt_params -- covers the
// PSTT RPCs that only exist under ENABLE_WALLET, which rpc_tests.cpp's
// non-wallet TestingSetup fixture never registers.
BOOST_FIXTURE_TEST_CASE(pstt_wallet_rpc_cli_param_conversion, PsttWalletTestingSetup)
{
    FundWalletWithP2PKHCoin(*this, 1 * COIN);
    CKey destKey;
    destKey.MakeNewKey(true);
    std::string destAddr = EncodeDestination(destKey.GetPubKey().GetID());

    // walletcreatefundedpstt: inputs(0)=array, outputs(1)=array/obj,
    // fallback_locktime(2)=num, options(3)=obj, bip32derivs(4)/
    // inputs_modifiable(5)/outputs_modifiable(6)=bool.
    BOOST_CHECK_THROW(CallPsttWalletRPCFromCli("walletcreatefundedpstt not_array {}"), std::runtime_error);
    UniValue funded;
    BOOST_CHECK_NO_THROW(funded = CallPsttWalletRPCFromCli(
        std::string("walletcreatefundedpstt [] {\"") + destAddr + "\":0.5} 0 {} false false false"));
    std::string pstt = funded.find_value("pstt").get_str();

    // walletupdatepstt: bip32derivs(1)=bool.
    BOOST_CHECK_THROW(CallPsttWalletRPCFromCli(std::string("walletupdatepstt ")+pstt+" not_bool"), std::runtime_error);
    UniValue updated;
    BOOST_CHECK_NO_THROW(updated = CallPsttWalletRPCFromCli(std::string("walletupdatepstt ")+pstt+" true"));

    // walletprocesspstt: sign(1)/bip32derivs(3)=bool.
    std::string updatedPstt = updated.find_value("pstt").get_str();
    BOOST_CHECK_THROW(CallPsttWalletRPCFromCli(std::string("walletprocesspstt ")+updatedPstt+" not_bool"), std::runtime_error);
    BOOST_CHECK_NO_THROW(CallPsttWalletRPCFromCli(std::string("walletprocesspstt ")+updatedPstt+" true ALL false ECDSA"));
}

// walletfundpsttfee's interactive mode doesn't always add a change output --
// CWallet::CreateTransaction folds change into the fee instead of creating
// an output for it whenever that change is dust (below the wallet's discard
// threshold, ~1820 tapyrus at the defaults used here -- see IsDust/
// GetDustThreshold and CreateTransaction's own dust-fold branch). Under a
// SIGHASH_SINGLE-marked PSTT, a fee input added with no paired change output
// is exactly the standalone input add that addinputtopstt itself already
// refuses in that state.
//
// CFeeRate(0) on the wallet is *not* a zero fee -- wallet/fees.cpp's
// GetMinimumFeeRate treats m_pay_tx_fee == CFeeRate(0) as "unset" and falls
// through to smart-fee estimation, which in a test environment with no
// mempool data lands on the much larger fallback fee (DEFAULT_FALLBACK_FEE)
// instead. CFeeRate(1) avoids that trap (nonzero, so the wallet honours it
// directly) while keeping the real fee negligible, well under the dust
// threshold above.
static const CAmount DUST_FOLD_MARGIN = 500; // tapyrus of headroom: comfortably covers the real (near-zero) fee, comfortably under the ~1820 dust threshold
static PartiallySignedTapyrusTransaction MakeSingleOutputPstt(CAmount amount, uint8_t modifiable)
{
    CKey destKey;
    destKey.MakeNewKey(true);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    // No inputs yet -- interactive mode's own coin selection must add one
    // from the wallet to cover the output below.
    PSTTOutput output;
    output.amount = amount;
    output.script = GetScriptForDestination(destKey.GetPubKey().GetID());
    pstt.outputs.push_back(output);
    pstt.tx_modifiable = modifiable;
    return pstt;
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_walletfundpsttfee_interactive_refuses_standalone_input_under_sighash_single, PsttWalletTestingSetup)
{
    sharedWallet->m_pay_tx_fee = CFeeRate(1);

    CTransactionRef fundingTx = FundWalletWithP2PKHCoin(*this, 1 * COIN);
    UniValue params(UniValue::VARR);
    params.push_back(EncodePSTT(MakeSingleOutputPstt(fundingTx->vout[0].nValue - DUST_FOLD_MARGIN, // leftover change is dust -- folded into fee, no change output
        PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE | PSTT_TXMOD_HAS_SIGHASH_SINGLE)));
    params.push_back(UniValue("interactive"));
    params.push_back(NullUniValue); // fee_input, ignored in interactive mode
    BOOST_CHECK_EXCEPTION(CallPsttWalletRPC("walletfundpsttfee", params), UniValue, [](const UniValue& e) {
        return e["message"].get_str().find("SIGHASH_SINGLE") != std::string::npos;
    });
}

// Same dust-fold, zero-change setup, but without SIGHASH_SINGLE -- confirms
// the guard is scoped to that state, not a blanket rejection of every
// zero-change interactive call. Uses its own fixture instance (rather than
// sharing one with the throw case above) so the wallet only ever has the one
// coin this case cares about -- coin selection otherwise happily combines an
// earlier, never-actually-spent coin with this one to cover the same
// target, which defeats the zero-change setup being tested.
BOOST_FIXTURE_TEST_CASE(pstt_rpc_walletfundpsttfee_interactive_allows_exact_match_without_sighash_single, PsttWalletTestingSetup)
{
    sharedWallet->m_pay_tx_fee = CFeeRate(1);

    CTransactionRef fundingTx = FundWalletWithP2PKHCoin(*this, 2 * COIN);
    UniValue params(UniValue::VARR);
    params.push_back(EncodePSTT(MakeSingleOutputPstt(fundingTx->vout[0].nValue - DUST_FOLD_MARGIN,
        PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE)));
    params.push_back(UniValue("interactive"));
    params.push_back(NullUniValue);
    UniValue funded;
    BOOST_CHECK_NO_THROW(funded = CallPsttWalletRPC("walletfundpsttfee", params));

    PartiallySignedTapyrusTransaction decoded;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(decoded, funded.get_str(), decodeError));
    BOOST_CHECK_EQUAL(decoded.inputs.size(), 1U); // the wallet's coin was selected
    BOOST_CHECK_EQUAL(decoded.outputs.size(), 1U); // and no change output was needed
}

// Carried over from the now-deleted psbt_wallet_tests.cpp (Phase 7, PSBT
// removal) -- ParseHDKeypath is a generic helper (AddKeypathToMap's own
// dependency) unrelated to the wire-format change, still exercised by
// PSTT's own *_BIP32_DERIVATION handling.
BOOST_AUTO_TEST_CASE(parse_hd_keypath)
{
    std::vector<uint32_t> keypath;

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1", keypath));
    BOOST_CHECK(!ParseHDKeypath("///////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1'/1", keypath));
    BOOST_CHECK(!ParseHDKeypath("//////////////////////////'/", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/", keypath));
    BOOST_CHECK(!ParseHDKeypath("1///////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1/1'/", keypath));
    BOOST_CHECK(!ParseHDKeypath("1/'//////////////////////////", keypath));

    BOOST_CHECK(ParseHDKeypath("", keypath));
    BOOST_CHECK(!ParseHDKeypath(" ", keypath));

    BOOST_CHECK(ParseHDKeypath("0", keypath));
    BOOST_CHECK(!ParseHDKeypath("O", keypath));

    BOOST_CHECK(ParseHDKeypath("0000'/0000'/0000'", keypath));
    BOOST_CHECK(!ParseHDKeypath("0000,/0000,/0000,", keypath));

    BOOST_CHECK(ParseHDKeypath("01234", keypath));
    BOOST_CHECK(!ParseHDKeypath("0x1234", keypath));

    BOOST_CHECK(ParseHDKeypath("1", keypath));
    BOOST_CHECK(!ParseHDKeypath(" 1", keypath));

    BOOST_CHECK(ParseHDKeypath("42", keypath));
    BOOST_CHECK(!ParseHDKeypath("m42", keypath));

    BOOST_CHECK(ParseHDKeypath("4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1

    BOOST_CHECK(ParseHDKeypath("m", keypath));
    BOOST_CHECK(!ParseHDKeypath("n", keypath));

    BOOST_CHECK(ParseHDKeypath("m/", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0'", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0''", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0'/0'", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/'0/0'", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("n/0/0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0/00", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0/0/f00", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/0/000000000000000000000000000000000000000000000000000000000000000000000000000000000000", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/1/1/111111111111111111111111111111111111111111111111111111111111111111111111111111111111", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/00/0", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/0'/00/'0", keypath));

    BOOST_CHECK(ParseHDKeypath("m/1/", keypath));
    BOOST_CHECK(!ParseHDKeypath("m/1//", keypath));

    BOOST_CHECK(ParseHDKeypath("m/0/4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("m/0/4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1

    BOOST_CHECK(ParseHDKeypath("m/4294967295", keypath)); // 4294967295 == 0xFFFFFFFF (uint32_t max)
    BOOST_CHECK(!ParseHDKeypath("m/4294967296", keypath)); // 4294967296 == 0xFFFFFFFF (uint32_t max) + 1
}

static CScript P2PKHScriptFor(const CPubKey& pubkey)
{
    CScript script;
    script << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    return script;
}

// doc/tapyrus/pstt.md's Updater role: the Updater must never change an
// input's sequence, neither for an input that's already signed nor for any
// input at all once some *other* input carries a SIGHASH_ALL-without-
// ANYONECANPAY signature. Unlike most other constraints, this isn't a
// conditional guard inside FillPSTT that rejects a mutation attempt --
// FillPSTT already never adds/removes/alters inputs at all, and that
// extends structurally to sequence: FillPSTT has no code path that ever
// writes PSTTInput::sequence, signed or not. So the right-sized test here is
// a regression tripwire proving that invariant holds across the specific
// state described above (one input fully SIGHASH_ALL-signed, another still
// unsigned), across repeated Updater/Signer passes -- not a
// conditional-branch test for a branch that doesn't exist.
BOOST_FIXTURE_TEST_CASE(pstt_updater_never_mutates_sequence, WalletTestingSetup)
{
    CKey keyA;
    keyA.MakeNewKey(true);
    CKey keyB; // deliberately never added to the wallet -- stays unsignable
    keyB.MakeNewKey(true);

    CMutableTransaction mtx1;
    mtx1.nFeatures = CTransaction::CURRENT_FEATURES;
    mtx1.vout.emplace_back(100000, P2PKHScriptFor(keyA.GetPubKey()));
    CTransactionRef prevTx1 = MakeTransactionRef(std::move(mtx1));
    CWalletTx wtx1(&m_wallet, prevTx1);
    m_wallet.mapWallet.emplace(wtx1.GetHash(), std::move(wtx1));
    {
        LOCK(m_wallet.cs_wallet);
        m_wallet.AddKeyPubKey(keyA, keyA.GetPubKey());
    }

    CMutableTransaction mtx2;
    mtx2.nFeatures = CTransaction::CURRENT_FEATURES;
    mtx2.vout.emplace_back(50000, P2PKHScriptFor(keyB.GetPubKey()));
    CTransactionRef prevTx2 = MakeTransactionRef(std::move(mtx2));
    CWalletTx wtx2(&m_wallet, prevTx2);
    m_wallet.mapWallet.emplace(wtx2.GetHash(), std::move(wtx2));
    // keyB intentionally not added -- prevTx2 is wallet-known (so the
    // Updater can attach its UTXO) but not wallet-signable, mirroring a
    // foreign input in an incremental multi-party PSTT.

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;

    PSTTInput input0;
    input0.previous_txid = prevTx1->GetHashMalFix();
    input0.prevout_index = 0;
    input0.previous_txid_set = true;
    input0.prevout_index_set = true;
    input0.sequence = 0xFFFFFFFE;
    pstt.inputs.push_back(input0);

    PSTTInput input1;
    input1.previous_txid = prevTx2->GetHashMalFix();
    input1.prevout_index = 0;
    input1.previous_txid_set = true;
    input1.prevout_index_set = true;
    input1.sequence = 0xFFFFFFFD;
    pstt.inputs.push_back(input1);

    CKey destKey;
    destKey.MakeNewKey(true);
    PSTTOutput output;
    output.amount = 140000;
    output.script = P2PKHScriptFor(destKey.GetPubKey());
    pstt.outputs.push_back(output);

    // First pass: Updater attaches both UTXOs, Signer completes input0
    // (SIGHASH_ALL, no ANYONECANPAY -- exactly the trigger condition for the
    // sequence-change restriction above) and leaves input1 unsigned (no
    // key). Sequences must be untouched.
    FillPSTT(&m_wallet, pstt, SIGHASH_ALL, /*sign=*/true, /*bip32derivs=*/false);
    BOOST_CHECK_EQUAL(*pstt.inputs[0].sequence, 0xFFFFFFFEU);
    BOOST_CHECK_EQUAL(*pstt.inputs[1].sequence, 0xFFFFFFFDU);
    BOOST_CHECK(!pstt.inputs[0].final_script_sig.empty());
    BOOST_CHECK(pstt.inputs[1].final_script_sig.empty());

    // Second pass: an Updater-only re-run over a PSTT that already carries
    // input0's SIGHASH_ALL-without-ANYONECANPAY signature -- the precise
    // state the restriction above guards against. Sequences must still be
    // untouched.
    FillPSTT(&m_wallet, pstt, SIGHASH_ALL, /*sign=*/false, /*bip32derivs=*/false);
    BOOST_CHECK_EQUAL(*pstt.inputs[0].sequence, 0xFFFFFFFEU);
    BOOST_CHECK_EQUAL(*pstt.inputs[1].sequence, 0xFFFFFFFDU);

    // Third pass: a further Signer re-attempt (still can't sign input1, no
    // key) -- sequences must still be untouched.
    FillPSTT(&m_wallet, pstt, SIGHASH_ALL, /*sign=*/true, /*bip32derivs=*/false);
    BOOST_CHECK_EQUAL(*pstt.inputs[0].sequence, 0xFFFFFFFEU);
    BOOST_CHECK_EQUAL(*pstt.inputs[1].sequence, 0xFFFFFFFDU);
}

BOOST_AUTO_TEST_SUITE_END()
