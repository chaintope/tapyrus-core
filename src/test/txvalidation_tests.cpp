// Copyright (c) 2017 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include <txmempool.h>
#include <amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/interpreter.h>
#include <test/test_tapyrus.h>
#include <policy/policy.h>
#include <coloridentifier.h>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(txvalidation_tests)

/**
 * Ensure that the mempool won't accept coinbase transactions.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_reject_coinbase, TestChainSetup)
{
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction coinbaseTx;

    coinbaseTx.nFeatures = 1;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vout.resize(1);
    coinbaseTx.vin[0].prevout.n = 1;
    coinbaseTx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    coinbaseTx.vout[0].nValue = 1 * CENT;
    coinbaseTx.vout[0].scriptPubKey = scriptPubKey;

    assert(CTransaction(coinbaseTx).IsCoinBase());

    CValidationState state;

    LOCK(cs_main);

    unsigned int initialPoolSize = mempool.size();

    CTxMempoolAcceptanceOptions opt;
    opt.flags = MempoolAcceptanceFlags::BYPASSS_LIMITS;
    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(MakeTransactionRef(coinbaseTx), opt));

    // Check that the transaction hasn't been added to mempool.
    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize);

    // Check that the validation state reflects the unsuccessful attempt.
    BOOST_CHECK(opt.state.IsInvalid());
    BOOST_CHECK_EQUAL(opt.state.GetRejectReason(), "coinbase");

    int nDoS;
    BOOST_CHECK_EQUAL(opt.state.IsInvalid(nDoS), true);
    BOOST_CHECK_EQUAL(nDoS, 100);
}
namespace
{
    const unsigned char vchKey0[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const unsigned char vchKey1[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    const unsigned char vchKey2[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};
    CKey key0, key1, key2;
    CPubKey pubkey0, pubkey1, pubkey2;
    std::vector<unsigned char> pubkeyHash0(20), pubkeyHash1(20), pubkeyHash2(20);
    void initKeys()
    {
        key0.Set(vchKey0, vchKey0 + 32, true);
        key1.Set(vchKey1, vchKey1 + 32, true);
        key2.Set(vchKey2, vchKey2 + 32, true);
        pubkey0 = key0.GetPubKey();
        pubkey1 = key1.GetPubKey();
        pubkey2 = key2.GetPubKey();
        CHash160().Write(pubkey0.data(), pubkey0.size()).Finalize(pubkeyHash0.data());
        CHash160().Write(pubkey1.data(), pubkey1.size()).Finalize(pubkeyHash1.data());
        CHash160().Write(pubkey2.data(), pubkey2.size()).Finalize(pubkeyHash2.data());
    }
}

void testTx(TestChainSetup* setup, const CTransactionRef tx, bool success, std::string errStr="")
{
    CTxMempoolAcceptanceOptions opt;
    opt.flags = MempoolAcceptanceFlags::BYPASSS_LIMITS;
    {
        LOCK(cs_main);

        BOOST_CHECK_EQUAL(
            success,
            AcceptToMemoryPool( tx, opt));
    }

    if(success)
    {
        BOOST_CHECK(opt.state.IsValid());
        std::vector<CMutableTransaction> txs;
        txs.push_back(CMutableTransaction(*tx));
        setup->CreateAndProcessBlock(txs, CScript() <<  ToByteVector(setup->coinbaseKey.GetPubKey()) << OP_CHECKSIG);
    }
    else if(opt.missingInputs.size())
    {
        BOOST_CHECK(!opt.state.IsInvalid());
    }
    else
    {
        BOOST_CHECK(opt.state.IsInvalid());
        BOOST_CHECK_EQUAL(opt.state.GetRejectReason(), errStr);
    }
}

void Sign(std::vector<unsigned char>& vchSig, CKey& signKey, const CScript& scriptPubKey, CMutableTransaction& inTx, int inIndex, CMutableTransaction& outTx, int outIndex)
{
    uint256 hash = SignatureHash(scriptPubKey, outTx, inIndex, SIGHASH_ALL, outTx.vout[outIndex].nValue, SigVersion::BASE);
    signKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
}

BOOST_FIXTURE_TEST_CASE(tx_invalid_token_issue, TestChainSetup)
{
    const unsigned char vchKey[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};
    CKey key;
    key.Set(vchKey, vchKey + 32, true);
    CPubKey pubkey = key.GetPubKey();
    std::vector<unsigned char> pubkeyHash(20);
    std::vector<unsigned char> vchPubKey(pubkey.begin(), pubkey.end());
    CHash160().Write(pubkey.data(), pubkey.size()).Finalize(pubkeyHash.data());

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nFeatures = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[3]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn, 0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(coinbaseSpendTx), true);

    // ColorIdentifier() serializes to 1 byte (NONE type, no payload), producing a
    // 28-byte script that is not structurally CP2PKH or CP2SH — non-standard.
    CScript scriptPubKey = CScript() << ColorIdentifier().toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    tokenIssueTx.nFeatures = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 100 * CENT;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    Sign(vchSig, key, coinbaseSpendTx.vout[0].scriptPubKey, coinbaseSpendTx, 0, tokenIssueTx, 0);
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey;

    testTx(this, MakeTransactionRef(tokenIssueTx), false, "bad-txns-nonstandard-opcolor");

    //test colorid in coinbase utxo
    //"CreateNewBlock: TestBlockValidity failed: bad-cb-issuetoken, coinbase cannot issue tokens"
    BOOST_CHECK_THROW(CreateAndProcessBlock({}, scriptPubKey), std::runtime_error);
}

/*
Test token type REISSUABLE
Txs:
coinbaseSpendTx
tokenIssueTx(from coinbaseSpendTx) - 100 tokens
tokenTransferTx - 1. no fee
                - 2. split into 0 + 100 tokens - invalid colorid
                - 3. add extra tokens into 50 + 60 tokens - token balance error
                - 4. success
test transaction reissue
tokenAggregateTx- 1. no fee
                - 2. add extra tokens - token balance error
                - 3. success
tokenBurnTx     - 1. no fee
                - 3. success
spend burnt token- failure
*/
BOOST_FIXTURE_TEST_CASE(tx_mempool_reissuable_token, TestChainSetup)
{
    initKeys();
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    std::vector<unsigned char> vchPubKey1(pubkey1.begin(), pubkey1.end());
    std::vector<unsigned char> vchPubKey2(pubkey2.begin(), pubkey2.end());

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nFeatures = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[2]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[2]->vout[0].scriptPubKey, coinbaseIn, 0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(coinbaseSpendTx), true);

    //token issue TYPE=1
    CScript scriptPubKey = CScript() << ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    //tokenIssueTx(from coinbaseSpendTx) - 100 tokens
    tokenIssueTx.nFeatures = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 100 * CENT;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, coinbaseSpendTx, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenIssueTx), true);

    //token transfer TYPE=1
    CScript scriptPubKey1 = CScript() << ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript scriptPubKey2 = CScript() << ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenTransferTx;

    //tokenTransferTx - 1. no fee
    tokenTransferTx.nFeatures = 1;
    tokenTransferTx.vin.resize(1);
    tokenTransferTx.vout.resize(2);
    tokenTransferTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenTransferTx.vin[0].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 50 * CENT;
    tokenTransferTx.vout[0].scriptPubKey = scriptPubKey1;
    tokenTransferTx.vout[1].nValue = 40 * CENT;
    tokenTransferTx.vout[1].scriptPubKey = scriptPubKey2;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-token-without-fee");

    //tokenTransferTx - 2. split into 0 + 100 tokens - token balance error
    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 0;
    tokenTransferTx.vout[1].nValue = 100 * CENT;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    CMutableTransaction coinbaseIn2(*m_coinbase_txns[3]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "invalid-colorid");

    //tokenTransferTx - 3. add extra tokens into 50 + 60 tokens - token balance error
    tokenTransferTx.vout[0].nValue = 50 * CENT;
    tokenTransferTx.vout[1].nValue = 60 * CENT;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-token-balance");

    //tokenTransferTx - 4. success
    tokenTransferTx.vout[1].nValue = 50 * CENT;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), true);

    //reissue same tokens - create input
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[1]->GetHashMalFix();

    Sign(vchSig, coinbaseKey, m_coinbase_txns[1]->vout[0].scriptPubKey, coinbaseIn, 0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(coinbaseSpendTx), true);

    //reissue same tokens
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, coinbaseSpendTx, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenIssueTx), true);

    CMutableTransaction tokenAggregateTx;
    //tokenAggregateTx - 1. no fee
    tokenAggregateTx.nFeatures = 1;
    tokenAggregateTx.vin.resize(3);
    tokenAggregateTx.vout.resize(1);
    tokenAggregateTx.vin[0].prevout.hashMalFix = tokenTransferTx.GetHashMalFix();
    tokenAggregateTx.vin[0].prevout.n = 0;
    tokenAggregateTx.vin[1].prevout.hashMalFix = tokenTransferTx.GetHashMalFix();
    tokenAggregateTx.vin[1].prevout.n = 1;
    tokenAggregateTx.vin[2].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenAggregateTx.vin[2].prevout.n = 0;
    tokenAggregateTx.vout[0].nValue = 200 * CENT;
    tokenAggregateTx.vout[0].scriptPubKey = scriptPubKey1;

    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, tokenTransferTx, 0, tokenAggregateTx, 0);
    tokenAggregateTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key2, tokenTransferTx.vout[1].scriptPubKey, tokenTransferTx, 1, tokenAggregateTx, 0);
    tokenAggregateTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey2;
    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 2, tokenAggregateTx, 0);
    tokenAggregateTx.vin[2].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenAggregateTx), false, "bad-txns-token-without-fee");

    //tokenAggregateTx - 2. add extra tokens - token balance error
    tokenAggregateTx.vin.resize(4);
    tokenAggregateTx.vin[3].prevout.hashMalFix = m_coinbase_txns[4]->GetHashMalFix();
    tokenAggregateTx.vin[3].prevout.n = 0;
    tokenAggregateTx.vout[0].nValue = 300 * CENT;

    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, tokenTransferTx, 0, tokenAggregateTx, 0);
    tokenAggregateTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key2, tokenTransferTx.vout[1].scriptPubKey, tokenTransferTx, 1, tokenAggregateTx, 0);
    tokenAggregateTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey2;
    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 2, tokenAggregateTx, 0);
    tokenAggregateTx.vin[2].scriptSig = CScript() << vchSig << vchPubKey0;
    CMutableTransaction coinbaseIn3(*m_coinbase_txns[4]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[4]->vout[0].scriptPubKey, coinbaseIn3, 3, tokenAggregateTx, 0);
    tokenAggregateTx.vin[3].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenAggregateTx), false, "bad-txns-token-balance");

    //tokenAggregateTx - 3. success
    tokenAggregateTx.vout[0].nValue = 200 * CENT;

    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, tokenTransferTx, 0, tokenAggregateTx, 0);
    tokenAggregateTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key2, tokenTransferTx.vout[1].scriptPubKey, tokenTransferTx, 1, tokenAggregateTx, 0);
    tokenAggregateTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey2;
    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 2, tokenAggregateTx, 0);
    tokenAggregateTx.vin[2].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[4]->vout[0].scriptPubKey, coinbaseIn3, 3, tokenAggregateTx, 0);
    tokenAggregateTx.vin[3].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenAggregateTx), true);

    CMutableTransaction tokenBurnTx;

    //tokenBurnTx - 1. no fee
    tokenBurnTx.nFeatures = 1;
    tokenBurnTx.vin.resize(1);
    tokenBurnTx.vout.resize(1);
    tokenBurnTx.vin[0].prevout.hashMalFix = tokenAggregateTx.GetHashMalFix();
    tokenBurnTx.vin[0].prevout.n = 0;
    tokenBurnTx.vout[0].nValue = 40 * CENT;
    tokenBurnTx.vout[0].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    Sign(vchSig, key0, tokenAggregateTx.vout[0].scriptPubKey, tokenAggregateTx, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;

    testTx(this, MakeTransactionRef(tokenBurnTx), false, "bad-txns-in-belowout");

    //tokenBurnTx - 2. success
    tokenBurnTx.vin.resize(2);
    tokenBurnTx.vin[1].prevout.hashMalFix = m_coinbase_txns[5]->GetHashMalFix();
    tokenBurnTx.vin[1].prevout.n = 0;

    Sign(vchSig, key1, tokenAggregateTx.vout[0].scriptPubKey, tokenAggregateTx, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    CMutableTransaction coinbaseIn5(*m_coinbase_txns[5]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[5]->vout[0].scriptPubKey, coinbaseIn5, 1, tokenBurnTx, 0);
    tokenBurnTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenBurnTx), true);

    //spend burnt token
    CMutableTransaction spendBurntTx;
    spendBurntTx.nFeatures = 1;
    spendBurntTx.vin.resize(2);
    spendBurntTx.vout.resize(1);
    spendBurntTx.vin[0].prevout.hashMalFix = tokenAggregateTx.GetHashMalFix();
    spendBurntTx.vin[0].prevout.n = 0;
    spendBurntTx.vin[1].prevout.hashMalFix = tokenBurnTx.GetHashMalFix();
    spendBurntTx.vin[1].prevout.n = 0;
    spendBurntTx.vout[0].nValue = 40 * CENT;
    spendBurntTx.vout[0].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    Sign(vchSig, key1, tokenAggregateTx.vout[0].scriptPubKey, tokenAggregateTx, 0, spendBurntTx, 0);
    spendBurntTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key0, tokenBurnTx.vout[0].scriptPubKey, tokenBurnTx, 0, spendBurntTx, 0);
    spendBurntTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey0;

    //missing inputs is set
    testTx(this, MakeTransactionRef(spendBurntTx), false, "");
}

/*
Test token type NON-REISSUABLE
Txs:
coinbaseSpendTx
tokenIssueTx(from coinbaseSpendTx) - 100 tokens
tokenTransferTx - 1. no fee
                - 2. split into 0 + 100 tokens - invalid colorid error
                - 3. add extra tokens into 50 + 60 tokens - token balance error
                - 4. success
test transaction reissue
tokenAggregateTx- 1. no fee
                - 2. add extra tokens - token balance error
                - 3. success
tokenBurnTx     - 1. no fee
                - 3. success
spend burnt token- failure
*/

BOOST_FIXTURE_TEST_CASE(tx_mempool_nonreissuable_token, TestChainSetup)
{
    initKeys();
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    std::vector<unsigned char> vchPubKey1(pubkey1.begin(), pubkey1.end());
    std::vector<unsigned char> vchPubKey2(pubkey2.begin(), pubkey2.end());

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nFeatures = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[2]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[2]->vout[0].scriptPubKey, coinbaseIn, 0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(coinbaseSpendTx), true);

    //token issue TYPE=2
    COutPoint utxo(coinbaseSpendTx.GetHashMalFix(), 0);
    ColorIdentifier colorid(utxo, TokenTypes::NON_REISSUABLE);
    CScript scriptPubKey = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    //tokenIssueTx(from coinbaseSpendTx) - 100 tokens
    tokenIssueTx.nFeatures = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 100 * CENT;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, coinbaseSpendTx, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenIssueTx), true);

    //token transfer TYPE=2
    CScript scriptPubKey1 = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript scriptPubKey2 = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenTransferTx;

    //tokenTransferTx - 1. no fee
    tokenTransferTx.nFeatures = 1;
    tokenTransferTx.vin.resize(1);
    tokenTransferTx.vout.resize(2);
    tokenTransferTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenTransferTx.vin[0].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 50 * CENT;
    tokenTransferTx.vout[0].scriptPubKey = scriptPubKey1;
    tokenTransferTx.vout[1].nValue = 40 * CENT;
    tokenTransferTx.vout[1].scriptPubKey = scriptPubKey2;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-token-without-fee");

    //tokenTransferTx  - 2. split into 0 + 100 tokens - token balance error
    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 0 * CENT;
    tokenTransferTx.vout[1].nValue = 100 * CENT;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    CMutableTransaction coinbaseIn2(*m_coinbase_txns[3]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "invalid-colorid");

    //tokenTransferTx - 3. add extra tokens into 50 + 60 tokens - token balance error
    tokenTransferTx.vout[0].nValue = 50 * CENT;
    tokenTransferTx.vout[1].nValue = 60 * CENT;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-token-balance");

    //tokenTransferTx - 4. success
    tokenTransferTx.vout[1].nValue = 50 * CENT;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), true);

    //reissue same tokens - create input
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[1]->GetHashMalFix();

    Sign(vchSig, coinbaseKey, m_coinbase_txns[1]->vout[0].scriptPubKey, coinbaseIn, 0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(coinbaseSpendTx), true);

    //reissue same tokens
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, coinbaseSpendTx, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenIssueTx), false, "invalid-colorid");

    CMutableTransaction tokenAggregateTx;
    //tokenAggregateTx - 1. no fee
    tokenAggregateTx.nFeatures = 1;
    tokenAggregateTx.vin.resize(2);
    tokenAggregateTx.vout.resize(1);
    tokenAggregateTx.vin[0].prevout.hashMalFix = tokenTransferTx.GetHashMalFix();
    tokenAggregateTx.vin[0].prevout.n = 0;
    tokenAggregateTx.vin[1].prevout.hashMalFix = tokenTransferTx.GetHashMalFix();
    tokenAggregateTx.vin[1].prevout.n = 1;
    tokenAggregateTx.vout[0].nValue = 100 * CENT;
    tokenAggregateTx.vout[0].scriptPubKey = scriptPubKey1;

    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, tokenTransferTx, 0, tokenAggregateTx, 0);
    tokenAggregateTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key2, tokenTransferTx.vout[1].scriptPubKey, tokenTransferTx, 1, tokenAggregateTx, 0);
    tokenAggregateTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey2;

    testTx(this, MakeTransactionRef(tokenAggregateTx), false, "bad-txns-token-without-fee");

    //tokenAggregateTx - 2. add extra tokens - token balance error
    tokenAggregateTx.vin.resize(3);
    tokenAggregateTx.vin[2].prevout.hashMalFix = m_coinbase_txns[4]->GetHashMalFix();
    tokenAggregateTx.vin[2].prevout.n = 0;
    tokenAggregateTx.vout[0].nValue = 300 * CENT;

    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, tokenTransferTx, 0, tokenAggregateTx, 0);
    tokenAggregateTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key2, tokenTransferTx.vout[1].scriptPubKey, tokenTransferTx, 1, tokenAggregateTx, 0);
    tokenAggregateTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey2;
    CMutableTransaction coinbaseIn3(*m_coinbase_txns[4]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[4]->vout[0].scriptPubKey, coinbaseIn3, 2, tokenAggregateTx, 0);
    tokenAggregateTx.vin[2].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenAggregateTx), false, "bad-txns-token-balance");

    //tokenAggregateTx - 3. success
    tokenAggregateTx.vout[0].nValue = 100 * CENT;

    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, tokenTransferTx, 0, tokenAggregateTx, 0);
    tokenAggregateTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key2, tokenTransferTx.vout[1].scriptPubKey, tokenTransferTx, 1, tokenAggregateTx, 0);
    tokenAggregateTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey2;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[4]->vout[0].scriptPubKey, coinbaseIn3, 2, tokenAggregateTx, 0);
    tokenAggregateTx.vin[2].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenAggregateTx), true);

    CMutableTransaction tokenBurnTx;

    //tokenBurnTx - 1. no fee
    tokenBurnTx.nFeatures = 1;
    tokenBurnTx.vin.resize(1);
    tokenBurnTx.vout.resize(1);
    tokenBurnTx.vin[0].prevout.hashMalFix = tokenAggregateTx.GetHashMalFix();
    tokenBurnTx.vin[0].prevout.n = 0;
    tokenBurnTx.vout[0].nValue = 40 * CENT;
    tokenBurnTx.vout[0].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    Sign(vchSig, key0, tokenAggregateTx.vout[0].scriptPubKey, tokenAggregateTx, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;

    testTx(this, MakeTransactionRef(tokenBurnTx), false, "bad-txns-in-belowout");

    //tokenBurnTx - 2. success
    tokenBurnTx.vin.resize(2);
    tokenBurnTx.vin[1].prevout.hashMalFix = m_coinbase_txns[5]->GetHashMalFix();
    tokenBurnTx.vin[1].prevout.n = 0;

    Sign(vchSig, key1, tokenAggregateTx.vout[0].scriptPubKey, tokenAggregateTx, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    CMutableTransaction coinbaseIn5(*m_coinbase_txns[5]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[5]->vout[0].scriptPubKey, coinbaseIn5, 1, tokenBurnTx, 0);
    tokenBurnTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenBurnTx), true);

    //spend burnt token
    CMutableTransaction spendBurntTx;
    spendBurntTx.nFeatures = 1;
    spendBurntTx.vin.resize(2);
    spendBurntTx.vout.resize(1);
    spendBurntTx.vin[0].prevout.hashMalFix = tokenAggregateTx.GetHashMalFix();
    spendBurntTx.vin[0].prevout.n = 0;
    spendBurntTx.vin[1].prevout.hashMalFix = tokenBurnTx.GetHashMalFix();
    spendBurntTx.vin[1].prevout.n = 0;
    spendBurntTx.vout[0].nValue = 40 * CENT;
    spendBurntTx.vout[0].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    Sign(vchSig, key1, tokenAggregateTx.vout[0].scriptPubKey, tokenAggregateTx, 0, spendBurntTx, 0);
    spendBurntTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key0, tokenBurnTx.vout[0].scriptPubKey, tokenBurnTx, 0, spendBurntTx, 0);
    spendBurntTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey0;

    //missing inputs is set
    testTx(this, MakeTransactionRef(spendBurntTx), false, "");
}

/*
Test token type NFT
Txs:
coinbaseSpendTx
tokenIssueTx(from coinbaseSpendTx) - 10000 tokens  - error
                                   - 1 token  - success
tokenTransferTx - 1. no fee
                - 2.  success
test transaction reissue
chande dust threshold and verify NFT output is not affected
tokenBurnTx     - 1. no fee
                - 3. success
spend burnt token - failure
*/

BOOST_FIXTURE_TEST_CASE(tx_mempool_nft_token, TestChainSetup)
{
    initKeys();
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    std::vector<unsigned char> vchPubKey1(pubkey1.begin(), pubkey1.end());
    std::vector<unsigned char> vchPubKey2(pubkey2.begin(), pubkey2.end());

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nFeatures = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[2]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[2]->vout[0].scriptPubKey, coinbaseIn, 0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(coinbaseSpendTx), true);

    //token issue TYPE=3
    COutPoint utxo(coinbaseSpendTx.GetHashMalFix(), 0);
    ColorIdentifier colorid(utxo, TokenTypes::NFT);
    CScript scriptPubKey = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    tokenIssueTx.nFeatures = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 10000;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, coinbaseSpendTx, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenIssueTx), false, "invalid-colorid");

    tokenIssueTx.vout[0].nValue = 1;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, coinbaseSpendTx, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenIssueTx), true);

    //token transfer TYPE=3
    CScript scriptPubKey1 = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript scriptPubKey2 = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenTransferTx;

    tokenTransferTx.nFeatures = 1;
    tokenTransferTx.vin.resize(1);
    tokenTransferTx.vout.resize(1);
    tokenTransferTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenTransferTx.vin[0].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 1;
    tokenTransferTx.vout[0].scriptPubKey = scriptPubKey1;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-token-without-fee");

    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;
    tokenTransferTx.vout.resize(2);
    tokenTransferTx.vout[1].nValue = 1;
    tokenTransferTx.vout[1].scriptPubKey = scriptPubKey2;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    CMutableTransaction coinbaseIn2(*m_coinbase_txns[3]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    // 2 NFT outputs with the same colorId: the per-colorId NFT output count check fires
    // first (bad-txns-nft-output-count) before the token balance check would be reached.
    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-nft-output-count");

    tokenTransferTx.vout[0].nValue = 1;
    tokenTransferTx.vout[1].nValue = 0;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    dustRelayFee = CFeeRate(1);
    testTx(this, MakeTransactionRef(tokenTransferTx), false, "invalid-colorid");

    dustRelayFee = CFeeRate(0);
    testTx(this, MakeTransactionRef(tokenTransferTx), false, "invalid-colorid");

    tokenTransferTx.vout.resize(1);
    tokenTransferTx.vout[0].nValue = 1;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), true);

    CMutableTransaction tokenBurnTx;

    //tokenBurnTx - 1. no fee
    tokenBurnTx.nFeatures = 1;
    tokenBurnTx.vin.resize(1);
    tokenBurnTx.vout.resize(1);
    tokenBurnTx.vin[0].prevout.hashMalFix = tokenTransferTx.GetHashMalFix();
    tokenBurnTx.vin[0].prevout.n = 0;
    tokenBurnTx.vout[0].nValue = 1;
    tokenBurnTx.vout[0].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, tokenTransferTx, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;

    testTx(this, MakeTransactionRef(tokenBurnTx), false, "bad-txns-in-belowout");

    //tokenBurnTx - 2. success
    tokenBurnTx.vin.resize(2);
    tokenBurnTx.vin[1].prevout.hashMalFix = m_coinbase_txns[5]->GetHashMalFix();
    tokenBurnTx.vin[1].prevout.n = 0;

    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, tokenTransferTx, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    CMutableTransaction coinbaseIn5(*m_coinbase_txns[5]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[5]->vout[0].scriptPubKey, coinbaseIn5, 1, tokenBurnTx, 0);
    tokenBurnTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenBurnTx), true);

    //spend burnt token
    CMutableTransaction spendBurntTx;
    spendBurntTx.nFeatures = 1;
    spendBurntTx.vin.resize(1);
    spendBurntTx.vout.resize(1);
    spendBurntTx.vin[0].prevout.hashMalFix = tokenTransferTx.GetHashMalFix();
    spendBurntTx.vin[0].prevout.n = 0;
    spendBurntTx.vout[0].nValue = 1;
    spendBurntTx.vout[0].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    Sign(vchSig, key2, tokenBurnTx.vout[0].scriptPubKey, tokenBurnTx, 0, spendBurntTx, 0);
    spendBurntTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey2;

    //missing inputs is set
    testTx(this, MakeTransactionRef(spendBurntTx), false, "");
}

/*
 * Test the single-swap colored script scenario from colored_coin_opcolor_validity:
 *
 *   scriptSig:   <validCid>
 *   scriptPubKey: <invalidC4> OP_SWAP OP_COLOR OP_DROP OP_1
 *
 * Execution: OP_SWAP brings validCid from scriptSig to TOS → OP_COLOR sets
 * color with validCid → OP_DROP removes c4 → OP_1 → success.
 *
 * The test checks which colorId is "seen by the outpoint":
 *   - GetColorIdFromScript(swapScript) → NONE (static pattern match fails)
 *   - VerifyTokenBalances treats the UTXO as uncolored (TPC)
 *   - OP_COLOR fires at runtime with validCid, but that does not change the
 *     static bookkeeping — the outpoint remains NONE from the validator's view.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_swap_color_script, TestChainSetup)
{
    initKeys();

    // Valid REISSUABLE colorId derived from pubkey0's P2PK script
    ColorIdentifier validCid(CScript() << ToByteVector(pubkey0) << OP_CHECKSIG);

    // Invalid c4 colorId (unsupported type byte 0xc4, non-zero payload)
    std::vector<unsigned char> invalidC4Cid(33);
    invalidC4Cid[0] = 0xc4;
    for (int i = 1; i < 33; i++) invalidC4Cid[i] = static_cast<unsigned char>(i);

    // Swap-based locking script:
    //   scriptSig must push <validCid>.
    //   [validCid, invalidC4] → OP_SWAP → [invalidC4, validCid] (validCid=TOS)
    //   OP_COLOR → color set with validCid → [invalidC4]
    //   OP_DROP → [] → OP_1 → [1] ✓
    CScript swapColorScript;
    swapColorScript << invalidC4Cid << OP_SWAP << OP_COLOR << OP_DROP << OP_1;

    // Step 1: Verify static analysis of the non-standard OP_COLOR script.
    //   IsColoredScript: OP_COLOR is present anywhere → true.
    //   GetColorIdFromScript: not CP2PKH, not CP2SH → NONE.
    BOOST_CHECK(swapColorScript.IsColoredScript());
    BOOST_CHECK(GetColorIdFromScript(swapColorScript).type == TokenTypes::NONE);

    // Step 2: Attempt to create a UTXO locked by this script.
    //   CheckColorIdentifierValidity: IsColoredScript()=true but not CP2PKH/CP2SH
    //   → "bad-txns-nonstandard-opcolor" — custom colored scripts are not permitted.
    CMutableTransaction fundTx;
    fundTx.nFeatures = 1;
    fundTx.vin.resize(1);
    fundTx.vout.resize(1);
    fundTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    fundTx.vin[0].prevout.n = 0;
    fundTx.vout[0].nValue = 100 * CENT;
    fundTx.vout[0].scriptPubKey = swapColorScript;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[2]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[2]->vout[0].scriptPubKey, coinbaseIn, 0, fundTx, 0);
    fundTx.vin[0].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(fundTx), false, "bad-txns-nonstandard-opcolor");
}

/*
 * tx_mempool_altstack_color_script
 *
 * Script: <nftCid(c3)> OP_TOALTSTACK <reissuableCid(c1)> OP_COLOR OP_FROMALTSTACK
 *
 * Execution (no scriptSig needed):
 *   push nftCid   → main: [nftCid],              alt: []
 *   OP_TOALTSTACK → main: [],                    alt: [nftCid]
 *   push c1       → main: [reissuableCid],        alt: [nftCid]
 *   OP_COLOR      → color set with c1(REISSUABLE) main: [],    alt: [nftCid]
 *   OP_FROMALTSTACK→main: [nftCid],              alt: []
 *   TOS = nftCid (33-byte, non-zero → truthy) → success
 *
 * Rejection reason: OP_COLOR is restricted to CP2PKH and CP2SH.  This script
 * uses altstack manipulation and does not match either standard form.
 * GetColorIdFromScript returns NONE → "bad-txns-nonstandard-opcolor".
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_altstack_color_script, TestChainSetup)
{
    initKeys();

    // c1 (REISSUABLE) colorId — derived from pubkey0's P2PK script
    ColorIdentifier reissuableCid(CScript() << ToByteVector(pubkey0) << OP_CHECKSIG);

    // c3 (NFT) colorId — derived from coinbase[3]'s outpoint
    COutPoint nftOutpoint(m_coinbase_txns[3]->GetHashMalFix(), 0);
    ColorIdentifier nftCid(nftOutpoint, TokenTypes::NFT);

    // Script: c3 → altstack; c1 set as color; c3 ← altstack (truthy TOS)
    CScript altStackColorScript;
    altStackColorScript << nftCid.toVector() << OP_TOALTSTACK
                        << reissuableCid.toVector() << OP_COLOR << OP_FROMALTSTACK;

    // Step 1: Static analysis.
    //   IsColoredScript: OP_COLOR is present → true.
    //   GetColorIdFromScript: not CP2PKH, not CP2SH → NONE.
    BOOST_CHECK(altStackColorScript.IsColoredScript());
    BOOST_CHECK(GetColorIdFromScript(altStackColorScript).type == TokenTypes::NONE);

    // Step 2: Attempt to fund a UTXO with this script as scriptPubKey.
    //   CheckColorIdentifierValidity: IsColoredScript()=true but not CP2PKH/CP2SH
    //   → DoS(100, "bad-txns-nonstandard-opcolor").
    CMutableTransaction fundTx;
    fundTx.nFeatures = 1;
    fundTx.vin.resize(1);
    fundTx.vout.resize(1);
    fundTx.vin[0].prevout.hashMalFix = m_coinbase_txns[4]->GetHashMalFix();
    fundTx.vin[0].prevout.n = 0;
    fundTx.vout[0].nValue = 100 * CENT;
    fundTx.vout[0].scriptPubKey = altStackColorScript;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[4]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[4]->vout[0].scriptPubKey, coinbaseIn, 0, fundTx, 0);
    fundTx.vin[0].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(fundTx), false, "bad-txns-nonstandard-opcolor");
}

/*
 * tx_mempool_drop_decoy_nft_script
 *
 * Script: <nftCid(c3)> <reissuableCid(c1)> OP_COLOR OP_DROP OP_1
 *
 * Execution (no scriptSig):
 *   push nftCid(c3)     → stack: [nftCid]
 *   push reissuableCid  → stack: [nftCid, reissuableCid]
 *   OP_COLOR            → color = c1(REISSUABLE), stack: [nftCid]
 *   OP_DROP             → stack: []
 *   OP_1                → stack: [1] → success (truthy TOS)
 *
 * Rejection: not CP2PKH, not CP2SH → "bad-txns-nonstandard-opcolor".
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_drop_decoy_nft_script, TestChainSetup)
{
    initKeys();

    // c1 (REISSUABLE) — derived from pubkey0's P2PK script
    ColorIdentifier reissuableCid(CScript() << ToByteVector(pubkey0) << OP_CHECKSIG);

    // c3 (NFT) — derived from a fixed outpoint (not actually spent here)
    COutPoint nftOutpoint(uint256S("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"), 0);
    ColorIdentifier nftCid(nftOutpoint, TokenTypes::NFT);

    // Script: c3 decoy pushed first, c1 color consumed by OP_COLOR, c3 dropped, OP_1 for truthy
    CScript dropDecoyNFT;
    dropDecoyNFT << nftCid.toVector() << reissuableCid.toVector() << OP_COLOR << OP_DROP << OP_1;

    // Step 1: Static analysis.
    //   IsColoredScript: OP_COLOR present → true.
    //   GetColorIdFromScript: not CP2PKH, not CP2SH → NONE.
    BOOST_CHECK(dropDecoyNFT.IsColoredScript());
    BOOST_CHECK(GetColorIdFromScript(dropDecoyNFT).type == TokenTypes::NONE);

    // Step 2: Attempt to create a UTXO locked by the decoy script.
    //   Rejected: CheckColorIdentifierValidity: IsColoredScript()=true but not CP2PKH/CP2SH
    //   → DoS(100, "bad-txns-nonstandard-opcolor").
    CMutableTransaction fundTx;
    fundTx.nFeatures = 1;
    fundTx.vin.resize(1);
    fundTx.vout.resize(1);
    fundTx.vin[0].prevout.hashMalFix = m_coinbase_txns[0]->GetHashMalFix();
    fundTx.vin[0].prevout.n = 0;
    fundTx.vout[0].nValue = 100 * CENT;
    fundTx.vout[0].scriptPubKey = dropDecoyNFT;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[0]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[0]->vout[0].scriptPubKey, coinbaseIn, 0, fundTx, 0);
    fundTx.vin[0].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(fundTx), false, "bad-txns-nonstandard-opcolor");
}

/*
 * tx_mempool_rot_decoy_script
 *
 * Script: <nftCid(c3)> <reissuableCid(c1)> OP_1 OP_ROT OP_COLOR OP_1
 *
 * Execution (no scriptSig):
 *   push nftCid     → stack: [nftCid]
 *   push reissuable → stack: [nftCid, reissuableCid]
 *   OP_1            → stack: [nftCid, reissuableCid, 1]
 *   OP_ROT          → stack: [reissuableCid, 1, nftCid]   (nftCid at TOS)
 *   OP_COLOR        → color = c3(NFT), stack: [reissuableCid, 1]
 *   OP_1            → stack: [reissuableCid, 1, 1] → success (truthy TOS)
 *
 * Rejection: not CP2PKH, not CP2SH → "bad-txns-nonstandard-opcolor".
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_rot_decoy_script, TestChainSetup)
{
    initKeys();

    // c1 (REISSUABLE) — derived from pubkey0's P2PK script
    ColorIdentifier reissuableCid(CScript() << ToByteVector(pubkey0) << OP_CHECKSIG);

    // c3 (NFT) — derived from a fixed outpoint
    COutPoint nftOutpoint(uint256S("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"), 0);
    ColorIdentifier nftCid(nftOutpoint, TokenTypes::NFT);

    // Script: c3 pushed, c1 pushed, OP_1, OP_ROT brings c3 to TOS for OP_COLOR
    CScript rotDecoy;
    rotDecoy << nftCid.toVector() << reissuableCid.toVector() << OP_1 << OP_ROT << OP_COLOR << OP_1;

    // Step 1: Static analysis.
    //   IsColoredScript: OP_COLOR present → true.
    //   GetColorIdFromScript: not CP2PKH, not CP2SH → NONE.
    BOOST_CHECK(rotDecoy.IsColoredScript());
    BOOST_CHECK(GetColorIdFromScript(rotDecoy).type == TokenTypes::NONE);

    // Step 2: Attempt to create a UTXO locked by the decoy script.
    //   Rejected: not CP2PKH/CP2SH → DoS(100, "bad-txns-nonstandard-opcolor").
    CMutableTransaction fundTx;
    fundTx.nFeatures = 1;
    fundTx.vin.resize(1);
    fundTx.vout.resize(1);
    fundTx.vin[0].prevout.hashMalFix = m_coinbase_txns[1]->GetHashMalFix();
    fundTx.vin[0].prevout.n = 0;
    fundTx.vout[0].nValue = 100 * CENT;
    fundTx.vout[0].scriptPubKey = rotDecoy;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[1]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[1]->vout[0].scriptPubKey, coinbaseIn, 0, fundTx, 0);
    fundTx.vin[0].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(fundTx), false, "bad-txns-nonstandard-opcolor");
}

/*
 * tx_mempool_drop_decoy_same_type_script
 *
 * Script: <decoy_c2(NON_REISSUABLE)> <real_c2(NON_REISSUABLE)> OP_COLOR OP_DROP OP_1
 *
 * Both colorIds have the same token type (c2/NON_REISSUABLE) but different outpoints.
 * The decoy c2 is pushed first and remains on the stack; the real c2 is consumed by
 * OP_COLOR; OP_DROP removes the decoy; OP_1 leaves a truthy value.
 *
 * Rejection: not CP2PKH, not CP2SH → "bad-txns-nonstandard-opcolor".
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_drop_decoy_same_type_script, TestChainSetup)
{
    initKeys();

    // real c2 (NON_REISSUABLE) — derived from a fixed outpoint
    COutPoint realOutpoint(uint256S("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"), 0);
    ColorIdentifier realNonReissuableCid(realOutpoint, TokenTypes::NON_REISSUABLE);

    // decoy c2 (NON_REISSUABLE) — different outpoint, same token type
    COutPoint decoyOutpoint(uint256S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), 1);
    ColorIdentifier decoyNonReissuableCid(decoyOutpoint, TokenTypes::NON_REISSUABLE);

    // Script: decoy_c2 pushed first, real_c2 consumed by OP_COLOR, decoy_c2 dropped, OP_1 for truthy
    CScript dropDecoySameType;
    dropDecoySameType << decoyNonReissuableCid.toVector() << realNonReissuableCid.toVector()
                      << OP_COLOR << OP_DROP << OP_1;

    // Step 1: Static analysis.
    //   IsColoredScript: OP_COLOR present → true.
    //   GetColorIdFromScript: not CP2PKH, not CP2SH → NONE.
    BOOST_CHECK(dropDecoySameType.IsColoredScript());
    BOOST_CHECK(GetColorIdFromScript(dropDecoySameType).type == TokenTypes::NONE);

    // Step 2: Attempt to create a UTXO locked by the same-type decoy script.
    //   Rejected: not CP2PKH/CP2SH → DoS(100, "bad-txns-nonstandard-opcolor").
    CMutableTransaction fundTx;
    fundTx.nFeatures = 1;
    fundTx.vin.resize(1);
    fundTx.vout.resize(1);
    fundTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    fundTx.vin[0].prevout.n = 0;
    fundTx.vout[0].nValue = 100 * CENT;
    fundTx.vout[0].scriptPubKey = dropDecoySameType;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn(*m_coinbase_txns[2]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[2]->vout[0].scriptPubKey, coinbaseIn, 0, fundTx, 0);
    fundTx.vin[0].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(fundTx), false, "bad-txns-nonstandard-opcolor");
}

/*
 * tx_mempool_cltv_colored_coin
 *
 * Part 1 — REJECTED: direct CLTV in a colored script.
 * Script: <colorId> OP_COLOR <locktime> OP_CLTV OP_DROP OP_DUP OP_HASH160 ... OP_CHECKSIG
 * Not CP2PKH or CP2SH → GetColorIdFromScript → NONE → "bad-txns-nonstandard-opcolor".
 *
 * Part 2 — ACCEPTED: CP2SH approach.
 * Redeem script: <locktime> OP_CLTV OP_DROP OP_DUP OP_HASH160 <pubkeyHash0> OP_EQUALVERIFY OP_CHECKSIG
 * Output script: <colorId> OP_COLOR OP_HASH160 <Hash160(redeemScript)> OP_EQUAL
 * This is a standard CP2SH colored output → GetColorIdFromScript returns colorId → accepted.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_cltv_colored_coin, TestChainSetup)
{
    initKeys();

    CScript coinbasePk = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    ColorIdentifier reissuableCid(coinbasePk);
    const int64_t locktime = 3;

    CScript cltvColorScript;
    cltvColorScript << reissuableCid.toVector() << OP_COLOR
                    << locktime << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                    << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0)
                    << OP_EQUALVERIFY << OP_CHECKSIG;

    // Script has OP_COLOR but is not CP2PKH or CP2SH → NONE.
    BOOST_CHECK(cltvColorScript.IsColoredScript());
    BOOST_CHECK(GetColorIdFromScript(cltvColorScript).type == TokenTypes::NONE);

    // Attempt to create a UTXO with this script → rejected.
    CMutableTransaction fundTx;
    fundTx.nFeatures = 1;
    fundTx.vin.resize(1);
    fundTx.vout.resize(1);
    fundTx.vin[0].prevout.hashMalFix = m_coinbase_txns[0]->GetHashMalFix();
    fundTx.vin[0].prevout.n = 0;
    fundTx.vout[0].nValue = 100 * CENT;
    fundTx.vout[0].scriptPubKey = cltvColorScript;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn0(*m_coinbase_txns[0]);
    Sign(vchSig, coinbaseKey, coinbasePk, coinbaseIn0, 0, fundTx, 0);
    fundTx.vin[0].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(fundTx), false, "bad-txns-nonstandard-opcolor");

    // Part 2: CP2SH colored output with CLTV inside the redeem script → accepted.
    CScript redeemScript;
    redeemScript << locktime << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                 << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0)
                 << OP_EQUALVERIFY << OP_CHECKSIG;

    CScriptID redeemScriptId(redeemScript);
    CScript cp2shColorScript = CScript() << reissuableCid.toVector() << OP_COLOR
                                         << OP_HASH160 << ToByteVector(redeemScriptId) << OP_EQUAL;

    BOOST_CHECK(cp2shColorScript.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(cp2shColorScript) == reissuableCid);

    CMutableTransaction cp2shFundTx;
    cp2shFundTx.nFeatures = 1;
    cp2shFundTx.vin.resize(1);
    cp2shFundTx.vout.resize(1);
    cp2shFundTx.vin[0].prevout.hashMalFix = m_coinbase_txns[1]->GetHashMalFix();
    cp2shFundTx.vin[0].prevout.n = 0;
    cp2shFundTx.vout[0].nValue = 100 * CENT;
    cp2shFundTx.vout[0].scriptPubKey = cp2shColorScript;

    CMutableTransaction coinbaseIn1(*m_coinbase_txns[1]);
    Sign(vchSig, coinbaseKey, coinbasePk, coinbaseIn1, 0, cp2shFundTx, 0);
    cp2shFundTx.vin[0].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(cp2shFundTx), true);
}

/*
 * Test the burn script (<colorId> OP_COLOR OP_TRUE) as an explicit burn destination.
 *
 * Txs:
 *   refillCoinbase(1) - mine block 6 → coinbase[5] (fresh, unspent)
 *   coinbaseSpendTx   - spend coinbase[5] → P2PKH (key0), 49 COIN out / 1 COIN fee
 *   tokenIssueTx      - coinbaseSpendTx → 100 REISSUABLE tokens (CP2PKH key0)
 *                       (mines block 7 → coinbase[6])
 *   tokenBurnTx       - 1. token balance mismatch (40 + 70 > 100) — rejected
 *                     - 2. success: 30 tokens → burn script, 70 tokens → CP2PKH key1
 *                       (mines block 8 → coinbase[7])
 *   spendChangeTx     - spend 70-token change from tokenBurnTx → CP2PKH key2
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_burn_script, TestChainSetup)
{
    initKeys();
    refillCoinbase(1); // mine block 6 → m_coinbase_txns[5]

    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    std::vector<unsigned char> vchPubKey1(pubkey1.begin(), pubkey1.end());
    std::vector<unsigned char> vchPubKey2(pubkey2.begin(), pubkey2.end());

    CScript tpcP2PKH0 = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    // coinbaseSpendTx: coinbase[5] → 49 COIN P2PKH key0 (1 COIN fee)
    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nFeatures = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[5]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 49 * COIN;
    coinbaseSpendTx.vout[0].scriptPubKey = tpcP2PKH0;

    std::vector<unsigned char> vchSig;
    CMutableTransaction coinbaseIn5(*m_coinbase_txns[5]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[5]->vout[0].scriptPubKey, coinbaseIn5, 0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(coinbaseSpendTx), true); // mines block 7 → coinbase[6]

    // Derive REISSUABLE colorId from the P2PKH locking script of coinbaseSpendTx output
    ColorIdentifier colorId(coinbaseSpendTx.vout[0].scriptPubKey);
    CScript cp2pkhKey0  = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;
    CScript cp2pkhKey1  = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;
    CScript cp2pkhKey2  = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CScript burnScript  = CScript() << colorId.toVector() << OP_COLOR << OP_TRUE;

    BOOST_CHECK(burnScript.IsColoredBurnScript());
    BOOST_CHECK(GetColorIdFromScript(burnScript) == colorId);

    // tokenIssueTx: coinbaseSpendTx → 100 tokens (CP2PKH key0)
    CMutableTransaction tokenIssueTx;
    tokenIssueTx.nFeatures = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 100 * CENT;
    tokenIssueTx.vout[0].scriptPubKey = cp2pkhKey0;

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, coinbaseSpendTx, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    testTx(this, MakeTransactionRef(tokenIssueTx), true); // mines block 8 → coinbase[7]

    // tokenBurnTx - 1. token balance mismatch: 40 + 70 > 100 input tokens → rejected
    CMutableTransaction tokenBurnTx;
    tokenBurnTx.nFeatures = 1;
    tokenBurnTx.vin.resize(2);
    tokenBurnTx.vout.resize(3);
    tokenBurnTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenBurnTx.vin[0].prevout.n = 0;
    tokenBurnTx.vin[1].prevout.hashMalFix = m_coinbase_txns[6]->GetHashMalFix();
    tokenBurnTx.vin[1].prevout.n = 0;
    tokenBurnTx.vout[0].nValue = 40 * CENT;    // burn script — intentionally too many
    tokenBurnTx.vout[0].scriptPubKey = burnScript;
    tokenBurnTx.vout[1].nValue = 70 * CENT;    // change — CP2PKH key1
    tokenBurnTx.vout[1].scriptPubKey = cp2pkhKey1;
    tokenBurnTx.vout[2].nValue = 49 * COIN;    // TPC change
    tokenBurnTx.vout[2].scriptPubKey = tpcP2PKH0;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    CMutableTransaction coinbaseIn6(*m_coinbase_txns[6]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[6]->vout[0].scriptPubKey, coinbaseIn6, 1, tokenBurnTx, 0);
    tokenBurnTx.vin[1].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(tokenBurnTx), false, "bad-txns-token-balance");

    // tokenBurnTx - 2. success: 30 to burn script + 70 change = 100 input tokens
    tokenBurnTx.vout[0].nValue = 30 * CENT;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[6]->vout[0].scriptPubKey, coinbaseIn6, 1, tokenBurnTx, 0);
    tokenBurnTx.vin[1].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(tokenBurnTx), true); // mines block 9 → coinbase[8]

    // spendChangeTx: spend 70-token CP2PKH change (key1) from tokenBurnTx → CP2PKH key2
    CMutableTransaction spendChangeTx;
    spendChangeTx.nFeatures = 1;
    spendChangeTx.vin.resize(2);
    spendChangeTx.vout.resize(2);
    spendChangeTx.vin[0].prevout.hashMalFix = tokenBurnTx.GetHashMalFix();
    spendChangeTx.vin[0].prevout.n = 1;
    spendChangeTx.vin[1].prevout.hashMalFix = m_coinbase_txns[7]->GetHashMalFix();
    spendChangeTx.vin[1].prevout.n = 0;
    spendChangeTx.vout[0].nValue = 70 * CENT;
    spendChangeTx.vout[0].scriptPubKey = cp2pkhKey2;
    spendChangeTx.vout[1].nValue = 49 * COIN;
    spendChangeTx.vout[1].scriptPubKey = tpcP2PKH0;

    Sign(vchSig, key1, tokenBurnTx.vout[1].scriptPubKey, tokenBurnTx, 0, spendChangeTx, 0);
    spendChangeTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    CMutableTransaction coinbaseIn7(*m_coinbase_txns[7]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[7]->vout[0].scriptPubKey, coinbaseIn7, 1, spendChangeTx, 0);
    spendChangeTx.vin[1].scriptSig = CScript() << vchSig;
    testTx(this, MakeTransactionRef(spendChangeTx), true);
}

BOOST_AUTO_TEST_SUITE_END()
