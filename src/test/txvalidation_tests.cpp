// Copyright (c) 2017 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
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

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(coinbaseTx),
                nullptr ,
                nullptr,
                true ,
                0));

    // Check that the transaction hasn't been added to mempool.
    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize);

    // Check that the validation state reflects the unsuccessful attempt.
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "coinbase");

    int nDoS;
    BOOST_CHECK_EQUAL(state.IsInvalid(nDoS), true);
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
    CValidationState state;
    bool pfMissingInputs;
    {
        LOCK(cs_main);

        BOOST_CHECK_EQUAL(
            success,
            AcceptToMemoryPool(mempool, state, tx,
                &pfMissingInputs ,
                nullptr,
                true ,
                0));
    }

    if(success)
    {
        BOOST_CHECK(state.IsValid());
        std::vector<CMutableTransaction> txs;
        txs.push_back(CMutableTransaction(*tx));
        setup->CreateAndProcessBlock(txs, CScript() <<  ToByteVector(setup->coinbaseKey.GetPubKey()) << OP_CHECKSIG);
    }
    else if(pfMissingInputs)
    {
        BOOST_CHECK(!state.IsInvalid());
    }
    else
    {
        BOOST_CHECK(state.IsInvalid());
        BOOST_CHECK_EQUAL(state.GetRejectReason(), errStr);
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

    testTx(this, MakeTransactionRef(tokenIssueTx), false, "invalid-colorid");

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
                - 2. split into 50 + 40 tokens - token balance error
                - 3. success
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

    //tokenTransferTx - 2. split into 50 + 40 tokens - token balance error
    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    CMutableTransaction coinbaseIn2(*m_coinbase_txns[3]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-token-balance");

    //tokenTransferTx - 3. success
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

    testTx(this, MakeTransactionRef(tokenBurnTx), false, "bad-txns-token-without-fee");

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
                - 2. split into 50 + 40 tokens - token balance error
                - 3. success
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

    //tokenTransferTx  - 2. split into 50 + 40 tokens - token balance error
    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    CMutableTransaction coinbaseIn2(*m_coinbase_txns[3]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-token-balance");

    //tokenTransferTx - 3. success
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

    testTx(this, MakeTransactionRef(tokenBurnTx), false, "bad-txns-token-without-fee");

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

    testTx(this, MakeTransactionRef(tokenTransferTx), false, "bad-txns-token-balance");

    tokenTransferTx.vout[0].nValue = 1;
    tokenTransferTx.vout[1].nValue = 0;

    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, tokenIssueTx, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseIn2, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    dustRelayFee = CFeeRate(1);
    testTx(this, MakeTransactionRef(tokenTransferTx), false, "dust");

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

    testTx(this, MakeTransactionRef(tokenBurnTx), false, "bad-txns-token-without-fee");

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

BOOST_AUTO_TEST_SUITE_END()
