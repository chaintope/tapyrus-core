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
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    // Check that the transaction hasn't been added to mempool.
    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize);

    // Check that the validation state reflects the unsuccessful attempt.
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "coinbase");

    int nDoS;
    BOOST_CHECK_EQUAL(state.IsInvalid(nDoS), true);
    BOOST_CHECK_EQUAL(nDoS, 100);
}

BOOST_FIXTURE_TEST_CASE(tx_mempool_reissuable_token, TestChainSetup)
{
    const unsigned char vchKey0[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const unsigned char vchKey1[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    const unsigned char vchKey2[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};
    CKey key0, key1, key2;
    key0.Set(vchKey0, vchKey0 + 32, true);
    key1.Set(vchKey1, vchKey1 + 32, true);
    key2.Set(vchKey2, vchKey2 + 32, true);
    CPubKey pubkey0 = key0.GetPubKey();
    CPubKey pubkey1 = key1.GetPubKey();
    CPubKey pubkey2 = key2.GetPubKey();
    std::vector<unsigned char> pubkeyHash0(20), pubkeyHash1(20), pubkeyHash2(20);
    CHash160().Write(pubkey0.data(), pubkey0.size()).Finalize(pubkeyHash0.data());
    CHash160().Write(pubkey1.data(), pubkey1.size()).Finalize(pubkeyHash1.data());
    CHash160().Write(pubkey2.data(), pubkey2.size()).Finalize(pubkeyHash2.data());

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nVersion = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    uint256 hash = SignatureHash(m_coinbase_txns[2]->vout[0].scriptPubKey, coinbaseSpendTx, 0, SIGHASH_ALL, coinbaseSpendTx.vout[0].nValue, SigVersion::BASE);
    std::vector<unsigned char> vchSig;
    coinbaseKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    LOCK(cs_main);
    CValidationState state;
    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(coinbaseSpendTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    std::vector<CMutableTransaction> txs;
    txs.push_back(coinbaseSpendTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);

    //token issue TYPE=1
    CScript scriptPubKey = CScript() << ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    tokenIssueTx.nVersion = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 100 * CENT;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    hash = SignatureHash(coinbaseSpendTx.vout[0].scriptPubKey, tokenIssueTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(tokenIssueTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state.IsValid());

    //confirm the transaction
    txs.clear();
    txs.push_back(tokenIssueTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);

    //token transfer TYPE=1
    CScript scriptPubKey1 = CScript() << ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript scriptPubKey2 = CScript() << ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenTransferTx;

    tokenTransferTx.nVersion = 1;
    tokenTransferTx.vin.resize(1);
    tokenTransferTx.vout.resize(2);
    tokenTransferTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenTransferTx.vin[0].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 50 * CENT;
    tokenTransferTx.vout[0].scriptPubKey = scriptPubKey1;
    tokenTransferTx.vout[1].nValue = 40 * CENT;
    tokenTransferTx.vout[1].scriptPubKey = scriptPubKey2;

    hash = SignatureHash(tokenIssueTx.vout[0].scriptPubKey, tokenTransferTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(tokenTransferTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-token-without-fee");

    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;

    hash = SignatureHash(tokenIssueTx.vout[0].scriptPubKey, tokenTransferTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    hash = SignatureHash(m_coinbase_txns[3]->vout[0].scriptPubKey, tokenTransferTx, 1, SIGHASH_ALL, m_coinbase_txns[3]->vout[0].nValue, SigVersion::BASE);
    coinbaseKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    CValidationState state2;
    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state2, MakeTransactionRef(tokenTransferTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state2.IsValid());
    //confirm the transaction
    txs.clear();
    txs.push_back(tokenTransferTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);
}


BOOST_FIXTURE_TEST_CASE(tx_mempool_invalid_token_issue, TestChainSetup)
{
    const unsigned char vchKey[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};
    CKey key;
    key.Set(vchKey, vchKey + 32, true);
    CPubKey pubkey = key.GetPubKey();
    std::vector<unsigned char> pubkeyHash(20);
    std::vector<unsigned char> vchPubKey(pubkey.begin(), pubkey.end());
    CHash160().Write(pubkey.data(), pubkey.size()).Finalize(pubkeyHash.data());

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nVersion = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<unsigned char> vchSig;
    uint256 hash = SignatureHash(m_coinbase_txns[3]->vout[0].scriptPubKey, coinbaseSpendTx, 0, SIGHASH_ALL, coinbaseSpendTx.vout[0].nValue, SigVersion::BASE);
    coinbaseKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    CValidationState state;

    LOCK(cs_main);
    unsigned int initialPoolSize = mempool.size();

    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(coinbaseSpendTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize + 1);
    std::vector<CMutableTransaction> txs;
    txs.push_back(coinbaseSpendTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);

    CScript scriptPubKey = CScript() << ColorIdentifier(TokenTypes::NONE).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    tokenIssueTx.nVersion = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 100 * CENT;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    hash = SignatureHash(coinbaseSpendTx.vout[0].scriptPubKey, tokenIssueTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey;

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(tokenIssueTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize);

    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "invalid-colorid");
}


BOOST_FIXTURE_TEST_CASE(tx_mempool_nonreissuable_token, TestChainSetup)
{
    const unsigned char vchKey0[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const unsigned char vchKey1[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    const unsigned char vchKey2[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};
    CKey key0, key1, key2;
    key0.Set(vchKey0, vchKey0 + 32, true);
    key1.Set(vchKey1, vchKey1 + 32, true);
    key2.Set(vchKey2, vchKey2 + 32, true);
    CPubKey pubkey0 = key0.GetPubKey();
    CPubKey pubkey1 = key1.GetPubKey();
    CPubKey pubkey2 = key2.GetPubKey();
    std::vector<unsigned char> pubkeyHash0(20), pubkeyHash1(20), pubkeyHash2(20);
    CHash160().Write(pubkey0.data(), pubkey0.size()).Finalize(pubkeyHash0.data());
    CHash160().Write(pubkey1.data(), pubkey1.size()).Finalize(pubkeyHash1.data());
    CHash160().Write(pubkey2.data(), pubkey2.size()).Finalize(pubkeyHash2.data());

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nVersion = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    uint256 hash = SignatureHash(m_coinbase_txns[2]->vout[0].scriptPubKey, coinbaseSpendTx, 0, SIGHASH_ALL, coinbaseSpendTx.vout[0].nValue, SigVersion::BASE);
    std::vector<unsigned char> vchSig;
    coinbaseKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    LOCK(cs_main);
    CValidationState state;
    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(coinbaseSpendTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    std::vector<CMutableTransaction> txs;
    txs.push_back(coinbaseSpendTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);

    //token issue TYPE=2
    COutPoint utxo(coinbaseSpendTx.GetHashMalFix(), 0);
    ColorIdentifier colorid(utxo, TokenTypes::NON_REISSUABLE);
    CScript scriptPubKey = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    tokenIssueTx.nVersion = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 100 * CENT;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    hash = SignatureHash(coinbaseSpendTx.vout[0].scriptPubKey, tokenIssueTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(tokenIssueTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state.IsValid());

    //confirm the transaction
    txs.clear();
    txs.push_back(tokenIssueTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);

    //token transfer TYPE=2
    CScript scriptPubKey1 = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript scriptPubKey2 = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenTransferTx;

    tokenTransferTx.nVersion = 1;
    tokenTransferTx.vin.resize(1);
    tokenTransferTx.vout.resize(2);
    tokenTransferTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenTransferTx.vin[0].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 50 * CENT;
    tokenTransferTx.vout[0].scriptPubKey = scriptPubKey1;
    tokenTransferTx.vout[1].nValue = 40 * CENT;
    tokenTransferTx.vout[1].scriptPubKey = scriptPubKey2;

    hash = SignatureHash(tokenIssueTx.vout[0].scriptPubKey, tokenTransferTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(tokenTransferTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-token-without-fee");

    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;

    hash = SignatureHash(tokenIssueTx.vout[0].scriptPubKey, tokenTransferTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    hash = SignatureHash(m_coinbase_txns[3]->vout[0].scriptPubKey, tokenTransferTx, 1, SIGHASH_ALL, m_coinbase_txns[3]->vout[0].nValue, SigVersion::BASE);
    coinbaseKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    CValidationState state2;
    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state2, MakeTransactionRef(tokenTransferTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state2.IsValid());
    //confirm the transaction
    txs.clear();
    txs.push_back(tokenTransferTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);
}

BOOST_FIXTURE_TEST_CASE(tx_mempool_nft_token, TestChainSetup)
{
    const unsigned char vchKey0[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    const unsigned char vchKey1[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    const unsigned char vchKey2[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};
    CKey key0, key1, key2;
    key0.Set(vchKey0, vchKey0 + 32, true);
    key1.Set(vchKey1, vchKey1 + 32, true);
    key2.Set(vchKey2, vchKey2 + 32, true);
    CPubKey pubkey0 = key0.GetPubKey();
    CPubKey pubkey1 = key1.GetPubKey();
    CPubKey pubkey2 = key2.GetPubKey();
    std::vector<unsigned char> pubkeyHash0(20), pubkeyHash1(20), pubkeyHash2(20);
    CHash160().Write(pubkey0.data(), pubkey0.size()).Finalize(pubkeyHash0.data());
    CHash160().Write(pubkey1.data(), pubkey1.size()).Finalize(pubkeyHash1.data());
    CHash160().Write(pubkey2.data(), pubkey2.size()).Finalize(pubkeyHash2.data());

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nVersion = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    uint256 hash = SignatureHash(m_coinbase_txns[2]->vout[0].scriptPubKey, coinbaseSpendTx, 0, SIGHASH_ALL, coinbaseSpendTx.vout[0].nValue, SigVersion::BASE);
    std::vector<unsigned char> vchSig;
    coinbaseKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    LOCK(cs_main);
    CValidationState state;
    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(coinbaseSpendTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    std::vector<CMutableTransaction> txs;
    txs.push_back(coinbaseSpendTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);

    //token issue TYPE=3
    COutPoint utxo(coinbaseSpendTx.GetHashMalFix(), 0);
    ColorIdentifier colorid(utxo, TokenTypes::NFT);
    CScript scriptPubKey = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    tokenIssueTx.nVersion = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 1;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    hash = SignatureHash(coinbaseSpendTx.vout[0].scriptPubKey, tokenIssueTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(tokenIssueTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state.IsValid());

    //confirm the transaction
    txs.clear();
    txs.push_back(tokenIssueTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);

    //token transfer TYPE=3
    CScript scriptPubKey1 = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript scriptPubKey2 = CScript() << colorid.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenTransferTx;

    tokenTransferTx.nVersion = 1;
    tokenTransferTx.vin.resize(1);
    tokenTransferTx.vout.resize(2);
    tokenTransferTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenTransferTx.vin[0].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 1;
    tokenTransferTx.vout[0].scriptPubKey = scriptPubKey1;
    tokenTransferTx.vout[1].nValue = 1;
    tokenTransferTx.vout[1].scriptPubKey = scriptPubKey2;

    hash = SignatureHash(tokenIssueTx.vout[0].scriptPubKey, tokenTransferTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(tokenTransferTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-token-without-fee");

    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;

    hash = SignatureHash(tokenIssueTx.vout[0].scriptPubKey, tokenTransferTx, 0, SIGHASH_ALL, tokenIssueTx.vout[0].nValue, SigVersion::BASE);
    key0.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    hash = SignatureHash(m_coinbase_txns[3]->vout[0].scriptPubKey, tokenTransferTx, 1, SIGHASH_ALL, m_coinbase_txns[3]->vout[0].nValue, SigVersion::BASE);
    coinbaseKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    CValidationState state2;
    BOOST_CHECK_EQUAL(
            true,
            AcceptToMemoryPool(mempool, state2, MakeTransactionRef(tokenTransferTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    BOOST_CHECK(state2.IsValid());
    //confirm the transaction
    txs.clear();
    txs.push_back(tokenTransferTx);
    CreateAndProcessBlock(txs, CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);
}

BOOST_AUTO_TEST_SUITE_END()
