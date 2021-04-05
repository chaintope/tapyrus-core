// Copyright (c) 2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/test/test_tapyrus_wallet.h>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <crypto/sha256.h>
#include <validation.h>
#include <net_processing.h>
#include <streams.h>
#include <script/sigcache.h>
#include <wallet/coincontrol.h>


/**
     * Import Coin to the wallet from coinbase output
     * @param amount
     * @return
     */
bool TestWalletSetup::ImportCoin(const CAmount amount) {
    CPubKey pubkey;
    wallet->GetKeyFromPool(pubkey, false);

    CMutableTransaction tx;
    tx.nFeatures = 1;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout.hashMalFix = m_coinbase_txns[0]->GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vout[0].nValue = amount;
    tx.vout[0].scriptPubKey = GetScriptForDestination({ pubkey.GetID() });

    std::vector<unsigned char> vchSig;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[0]->vout[0].scriptPubKey, 0, tx, 0);
    tx.vin[0].scriptSig = CScript() << vchSig;

    if (!AddToWalletAndMempool(MakeTransactionRef(tx))) {
        return false;
    }

    if (!ProcessBlockAndScanForWalletTxns(MakeTransactionRef(tx))) {
        return false;
    }
    return true;
}

void TestWalletSetup::initWallet()
{
    std::vector<unsigned char> coinbasePubKeyHash(20);
    CPubKey coinbasePubKey = coinbaseKey.GetPubKey();
    CHash160().Write(coinbasePubKey.data(), coinbasePubKey.size()).Finalize(
            coinbasePubKeyHash.data());

    wallet = MakeUnique<CWallet>("mock", WalletDatabase::CreateMock());
    bool firstRun;
    wallet->LoadWallet(firstRun);
}

bool TestWalletSetup::ProcessBlockAndScanForWalletTxns(const CTransactionRef tx) {
    CValidationState state;
    bool pfMissingInputs;
    {
        LOCK(cs_main);
        if(!AcceptToMemoryPool(mempool, state, tx, &pfMissingInputs, nullptr, true, 0)) {
            return false;
        }
    }
    CreateAndProcessBlock({ CMutableTransaction(*tx) }, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);

    WalletRescanReserver reserver(wallet.get());
    reserver.reserve();

    if(wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver, true)) {
        return false;
    }

    return true;
}

bool TestWalletSetup::AddToWalletAndMempool(const CTransactionRef tx) {
    CWalletTx wtx(wallet.get(), tx);
    if(!wallet->AddToWallet(wtx)) {
        return false;
    }
    wallet->TransactionAddedToMempool(tx);

    return true;
}

void TestWalletSetup::Sign(std::vector<unsigned char>& vchSig, CKey& signKey, const CScript& scriptPubKey, int inIndex, CMutableTransaction& outTx, int outIndex)
{
    uint256 hash = SignatureHash(scriptPubKey, outTx, inIndex, SIGHASH_ALL, outTx.vout[outIndex].nValue, SigVersion::BASE);
    signKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
}

bool TestWalletSetup::IssueNonReissunableColoredCoin(const CAmount amount, ColorIdentifier& cid) {
    CPubKey pubkey;
    wallet->GetKeyFromPool(pubkey, false);

    // create UTXO and color id for issue
    CCoinControl coinControl;
    CReserveKey reservekey(wallet.get());
    CAmount nFeeRequired;
    std::string strError;
    CWallet::ChangePosInOut mapChangePosRet;
    mapChangePosRet[ColorIdentifier()] = -1;
    std::vector<CRecipient> vecSend;
    CScript scriptpubkey = GetScriptForDestination({ pubkey.GetID() });
    vecSend.push_back({scriptpubkey, 1 * CENT, false});
    CTransactionRef tx;
    if (!wallet->CreateTransaction(vecSend, tx, reservekey, nFeeRequired, mapChangePosRet, strError, coinControl)) {
        return false;
    }
    auto i = std::find_if(tx->vout.begin(), tx->vout.end(), [&]( const CTxOut &out ) {
        return out.scriptPubKey == scriptpubkey;
    }) - tx->vout.begin();

    // tx should be hold in the wallet before use its outputs for next transactions
    if (!AddToWalletAndMempool(tx)) {
        return false;
    }

    COutPoint out(tx->GetHashMalFix(), i);
    ColorIdentifier colorId(out, TokenTypes::NON_REISSUABLE);
    cid = colorId;

    CColorKeyID colorkeyid({ pubkey.GetID() }, colorId);
    CMutableTransaction issueTx;
    issueTx.nFeatures = 1;
    issueTx.vin.resize(1);
    issueTx.vout.resize(1);
    issueTx.vin[0].prevout = out;
    issueTx.vout[0].nValue = amount;
    issueTx.vout[0].scriptPubKey = GetScriptForDestination(colorkeyid);
    {
        LOCK(wallet->cs_wallet);
        if(!wallet->SignTransaction(issueTx)) {
            return false;
        }
    }


    if (!AddToWalletAndMempool(MakeTransactionRef(issueTx))) {
        return false;
    }

    if (!ProcessBlockAndScanForWalletTxns(tx) || !ProcessBlockAndScanForWalletTxns(MakeTransactionRef(issueTx))) {
        return false;
    }

    return true;
}