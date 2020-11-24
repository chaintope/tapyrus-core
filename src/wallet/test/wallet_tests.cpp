// Copyright (c) 2012-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <memory>
#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

#include <consensus/validation.h>
#include <rpc/server.h>
#include <rpc/client.h>
#include <test/test_tapyrus.h>
#include <validation.h>
#include <rpc/blockchain.cpp>
#include <wallet/coincontrol.h>
#include <wallet/test/wallet_test_fixture.h>
#include <utilstrencodings.h>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>
#include <univalue.h>

extern UniValue importmulti(const JSONRPCRequest& request);
extern UniValue dumpwallet(const JSONRPCRequest& request);
extern UniValue importwallet(const JSONRPCRequest& request);
extern UniValue generate(const JSONRPCRequest& request);

BOOST_FIXTURE_TEST_SUITE(wallet_tests, WalletTestingSetup)

static void AddKey(CWallet& wallet, const CKey& key)
{
    LOCK(wallet.cs_wallet);
    wallet.AddKeyPubKey(key, key.GetPubKey());
}

UniValue CallGenerate(const JSONRPCRequest& request)
{
    try {
        UniValue result = generate(request);
        return result;
    }
    catch (const UniValue& objError) {
        throw std::runtime_error(find_value(objError, "message").get_str());
    }
}

BOOST_FIXTURE_TEST_CASE(rescan, TestChainSetup)
{
    // Cap last block file size, and mine new block in a new block file.
    CBlockIndex* const nullBlock = nullptr;
    CBlockIndex* oldTip = chainActive.Tip();
    GetBlockFileInfo(oldTip->GetBlockPos().nFile)->nSize = MAX_BLOCKFILE_SIZE;
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    CBlockIndex* newTip = chainActive.Tip();

    LOCK(cs_main);

    // Verify ScanForWalletTransactions picks up transactions in both the old
    // and new block files.
    {
        CWallet wallet("dummy", WalletDatabase::CreateDummy());
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(&wallet);
        reserver.reserve();
        BOOST_CHECK_EQUAL(nullBlock, wallet.ScanForWalletTransactions(oldTip, nullptr, reserver));
        BOOST_CHECK_EQUAL(wallet.GetBalance()[ColorIdentifier()], 100 * COIN);
    }

    // Prune the older block file.
    PruneOneBlockFile(oldTip->GetBlockPos().nFile);
    UnlinkPrunedFiles({oldTip->GetBlockPos().nFile});

    // Verify ScanForWalletTransactions only picks transactions in the new block
    // file.
    {
        CWallet wallet("dummy", WalletDatabase::CreateDummy());
        AddKey(wallet, coinbaseKey);
        WalletRescanReserver reserver(&wallet);
        reserver.reserve();
        BOOST_CHECK_EQUAL(oldTip, wallet.ScanForWalletTransactions(oldTip, nullptr, reserver));
        BOOST_CHECK_EQUAL(wallet.GetBalance()[ColorIdentifier()], 50 * COIN);
    }

    // Verify importmulti RPC returns failure for a key whose creation time is
    // before the missing block, and success for a key whose creation time is
    // after.
    {
        std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>("dummy", WalletDatabase::CreateDummy());
        AddWallet(wallet);
        UniValue keys;
        keys.setArray();
        UniValue key;
        key.setObject();
        key.pushKV("scriptPubKey", HexStr(GetScriptForRawPubKey(coinbaseKey.GetPubKey())));
        key.pushKV("timestamp", 0);
        key.pushKV("internal", UniValue(true));
        keys.push_back(key);
        key.clear();
        key.setObject();
        CKey futureKey;
        futureKey.MakeNewKey(true);
        key.pushKV("scriptPubKey", HexStr(GetScriptForRawPubKey(futureKey.GetPubKey())));
        key.pushKV("timestamp", newTip->GetBlockTimeMax() + TIMESTAMP_WINDOW + 1);
        key.pushKV("internal", UniValue(true));
        keys.push_back(key);
        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back(keys);

        UniValue response = importmulti(request);
        BOOST_CHECK_EQUAL(response.write(),
            strprintf("[{\"success\":false,\"error\":{\"code\":-1,\"message\":\"Rescan failed for key with creation "
                      "timestamp %d. There was an error reading a block from time %d, which is after or within %d "
                      "seconds of key creation, and could contain transactions pertaining to the key. As a result, "
                      "transactions and coins using this key may not appear in the wallet. This error could be caused "
                      "by pruning or data corruption (see bitcoind log for details) and could be dealt with by "
                      "downloading and rescanning the relevant blocks (see -reindex and -rescan "
                      "options).\"}},{\"success\":true}]",
                              0, oldTip->GetBlockTimeMax(), TIMESTAMP_WINDOW));
        RemoveWallet(wallet);
    }
}

// Verify importwallet RPC starts rescan at earliest block with timestamp
// greater or equal than key birthday. Previously there was a bug where
// importwallet RPC would start the scan at the latest block with timestamp less
// than or equal to key birthday.
BOOST_FIXTURE_TEST_CASE(importwallet_rescan, TestChainSetup)
{
    // Create two blocks with same timestamp to verify that importwallet rescan
    // will pick up both blocks, not just the first.
    const int64_t BLOCK_TIME = chainActive.Tip()->GetBlockTimeMax() + 5;
    SetMockTime(BLOCK_TIME);
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    // Set key birthday to block time increased by the timestamp window, so
    // rescan will start at the block time.
    const int64_t KEY_TIME = BLOCK_TIME + TIMESTAMP_WINDOW;
    SetMockTime(KEY_TIME);
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));

    LOCK(cs_main);

    std::string backup_file = (SetDataDir("importwallet_rescan") / "wallet.backup").string();

    // Import key into wallet and call dumpwallet to create backup file.
    {
        std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>("dummy", WalletDatabase::CreateDummy());
        LOCK(wallet->cs_wallet);
        wallet->mapKeyMetadata[coinbaseKey.GetPubKey().GetID()].nCreateTime = KEY_TIME;
        wallet->AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back(backup_file);
        AddWallet(wallet);
        ::dumpwallet(request);
        RemoveWallet(wallet);
    }

    // Call importwallet RPC and verify all blocks with timestamps >= BLOCK_TIME
    // were scanned, and no prior blocks were scanned.
    {
        std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>("dummy", WalletDatabase::CreateDummy());

        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back(backup_file);
        AddWallet(wallet);
        ::importwallet(request);
        RemoveWallet(wallet);

        LOCK(wallet->cs_wallet);
        BOOST_CHECK_EQUAL(wallet->mapWallet.size(), 3U);
        BOOST_CHECK_EQUAL(m_coinbase_txns.size(), 8U);
        for (size_t i = 0; i < m_coinbase_txns.size(); ++i) {
            bool found = wallet->GetWalletTx(m_coinbase_txns[i]->GetHashMalFix());
            bool expected = i >= 5;
            BOOST_CHECK_EQUAL(found, expected);
        }
    }

    SetMockTime(0);
}

static int64_t AddTx(CWallet& wallet, uint32_t lockTime, int64_t mockTime, int64_t blockTime)
{
    CMutableTransaction tx;
    tx.nLockTime = lockTime;
    SetMockTime(mockTime);
    CBlockIndex* block = nullptr;
    if (blockTime > 0) {
        LOCK(cs_main);
        auto inserted = mapBlockIndex.emplace(GetRandHash(), new CBlockIndex);
        assert(inserted.second);
        const uint256& hash = inserted.first->first;
        block = inserted.first->second;
        block->nTime = blockTime;
        block->phashBlock = &hash;
    }

    CWalletTx wtx(&wallet, MakeTransactionRef(tx));
    if (block) {
        wtx.SetMerkleBranch(block, 0);
    }
    {
        LOCK(cs_main);
        wallet.AddToWallet(wtx);
    }
    LOCK(wallet.cs_wallet);
    return wallet.mapWallet.at(wtx.GetHash()).nTimeSmart;
}

// Simple test to verify assignment of CWalletTx::nSmartTime value. Could be
// expanded to cover more corner cases of smart time logic.
BOOST_AUTO_TEST_CASE(ComputeTimeSmart)
{
    // New transaction should use clock time if lower than block time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 1, 100, 120), 100);

    // Test that updating existing transaction does not change smart time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 1, 200, 220), 100);

    // New transaction should use clock time if there's no block time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 2, 300, 0), 300);

    // New transaction should use block time if lower than clock time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 3, 420, 400), 400);

    // New transaction should use latest entry time if higher than
    // min(block time, clock time).
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 4, 500, 390), 400);

    // If there are future entries, new transaction should use time of the
    // newest entry that is no more than 300 seconds ahead of the clock time.
    BOOST_CHECK_EQUAL(AddTx(m_wallet, 5, 50, 600), 300);

    // Reset mock time for other tests.
    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(LoadReceiveRequests)
{
    CTxDestination dest = CKeyID();
    LOCK(m_wallet.cs_wallet);
    m_wallet.AddDestData(dest, "misc", "val_misc");
    m_wallet.AddDestData(dest, "rr0", "val_rr0");
    m_wallet.AddDestData(dest, "rr1", "val_rr1");

    auto values = m_wallet.GetDestValues("rr");
    BOOST_CHECK_EQUAL(values.size(), 2U);
    BOOST_CHECK_EQUAL(values[0], "val_rr0");
    BOOST_CHECK_EQUAL(values[1], "val_rr1");
}

class ListCoinsTestingSetup : public TestChainSetup
{
public:
    ListCoinsTestingSetup()
    {
        CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
        wallet = MakeUnique<CWallet>("mock", WalletDatabase::CreateMock());
        bool firstRun;
        wallet->LoadWallet(firstRun);
        AddKey(*wallet, coinbaseKey);
        WalletRescanReserver reserver(wallet.get());
        reserver.reserve();
        wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);
    }

    ~ListCoinsTestingSetup()
    {
        wallet.reset();
    }

    CWalletTx& AddTx(CRecipient recipient)
    {
        CTransactionRef tx;
        CReserveKey reservekey(wallet.get());
        CAmount fee;
        int changePos = -1;
        std::string error;
        CCoinControl dummy;
        BOOST_CHECK(wallet->CreateTransaction({recipient}, tx, reservekey, fee, changePos, error, dummy));
        CValidationState state;
        BOOST_CHECK(wallet->CommitTransaction(tx, {}, {}, {}, reservekey, nullptr, state));
        CMutableTransaction blocktx;
        {
            LOCK(wallet->cs_wallet);
            blocktx = CMutableTransaction(*wallet->mapWallet.at(tx->GetHashMalFix()).tx);
        }
        CreateAndProcessBlock({CMutableTransaction(blocktx)}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
        LOCK(wallet->cs_wallet);
        auto it = wallet->mapWallet.find(tx->GetHashMalFix());
        BOOST_CHECK(it != wallet->mapWallet.end());
        it->second.SetMerkleBranch(chainActive.Tip(), 1);
        return it->second;
    }

    std::unique_ptr<CWallet> wallet;
};

BOOST_FIXTURE_TEST_CASE(ListCoins, ListCoinsTestingSetup)
{
    std::string coinbaseAddress = coinbaseKey.GetPubKey().GetID().ToString();

    // Confirm ListCoins returns 2 coin grouped under coinbaseKey
    // address.
    auto list = wallet->ListCoins();
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 6U);

    // Check initial balance from 6 coinbase transaction.
    BOOST_CHECK_EQUAL(300 * COIN, wallet->GetAvailableBalance()[ColorIdentifier()]);

    // Add a transaction creating a change address, and confirm ListCoins still
    // returns the coin associated with the change address underneath the
    // coinbaseKey pubkey, even though the change address has a different
    // pubkey.
    AddTx(CRecipient{GetScriptForRawPubKey({}), 1 * COIN, false /* subtract fee */});
    list = wallet->ListCoins();
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 6U);

    // Lock both coins. Confirm number of available coins drops to 0.
    {
        LOCK2(cs_main, wallet->cs_wallet);
        std::vector<COutput> available;
        wallet->AvailableCoins(available);
        BOOST_CHECK_EQUAL(available.size(), 6U);
    }
    for (const auto& group : list) {
        for (const auto& coin : group.second) {
            LOCK(wallet->cs_wallet);
            wallet->LockCoin(COutPoint(coin.tx->GetHash(), coin.i));
        }
    }
    {
        LOCK2(cs_main, wallet->cs_wallet);
        std::vector<COutput> available;
        wallet->AvailableCoins(available);
        BOOST_CHECK_EQUAL(available.size(), 0U);
    }
    // Confirm ListCoins still returns same result as before, despite coins
    // being locked.
    list = wallet->ListCoins();
    BOOST_CHECK_EQUAL(list.size(), 1U);
    BOOST_CHECK_EQUAL(boost::get<CKeyID>(list.begin()->first).ToString(), coinbaseAddress);
    BOOST_CHECK_EQUAL(list.begin()->second.size(), 6U);
}

BOOST_FIXTURE_TEST_CASE(wallet_disableprivkeys, TestChainSetup)
{
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>("dummy", WalletDatabase::CreateDummy());
    wallet->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    BOOST_CHECK(!wallet->TopUpKeyPool(1000));
    CPubKey pubkey;
    BOOST_CHECK(!wallet->GetKeyFromPool(pubkey, false));
}

// Check that UniValue generate(const JSONRPCRequest& request) with privkey generate process.
//
BOOST_FIXTURE_TEST_CASE(generate_with_incorrect_privkey, TestingSetup)
{
    // convert check
    UniValue result;
    UniValue ar = UniValue(UniValue::VARR);
    BOOST_CHECK_NO_THROW(result = RPCConvertValues("generate", {"101", "\"c87509a1c067bbde78beb793e6fa76530b6382a4c0241e5e4a9ec0a0f44dc0d3\""}));
    BOOST_CHECK_EQUAL(result[0].get_int(), 101);
    BOOST_CHECK_EQUAL(result[1].get_str(), "\"c87509a1c067bbde78beb793e6fa76530b6382a4c0241e5e4a9ec0a0f44dc0d3\"");

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

BOOST_FIXTURE_TEST_CASE(ismine_wallet_tokentx, TestChainSetup) {
    initKeys();
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    std::vector<unsigned char> vchPubKey1(pubkey1.begin(), pubkey1.end());
    std::vector<unsigned char> vchPubKey2(pubkey2.begin(), pubkey2.end());

    // create a dummy wallet add key pubkey
    CWallet wallet("dummy", WalletDatabase::CreateDummy());
    bool firstRun;
    wallet.LoadWallet(firstRun);
    LOCK(cs_main);
    LOCK(wallet.cs_wallet);
    wallet.AddKeyPubKey(key0, pubkey0);
    WalletRescanReserver reserver(&wallet);
    reserver.reserve();

    // create first coinbase tx
    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nFeatures = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    CWalletTx coinbasewtx(&wallet, MakeTransactionRef(coinbaseSpendTx));
    wallet.AddToWallet(coinbasewtx);
    BOOST_CHECK_EQUAL(wallet.IsMine(coinbaseSpendTx.vout[0]), ISMINE_SPENDABLE);

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

    CWalletTx tokenwtx(&wallet, MakeTransactionRef(tokenIssueTx));
    wallet.AddToWallet(tokenwtx);
    BOOST_CHECK_EQUAL(wallet.IsMine(tokenIssueTx.vout[0]), ISMINE_SPENDABLE);

    //token transfer TYPE=1
    CScript scriptPubKey1 = CScript() << ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript scriptPubKey2 = CScript() << ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenTransferTx;

    //tokenTransferTx
    tokenTransferTx.nFeatures = 1;
    tokenTransferTx.vin.resize(1);
    tokenTransferTx.vout.resize(2);
    tokenTransferTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenTransferTx.vin[0].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 50 * CENT;
    tokenTransferTx.vout[0].scriptPubKey = scriptPubKey1;
    tokenTransferTx.vout[1].nValue = 50 * CENT;
    tokenTransferTx.vout[1].scriptPubKey = scriptPubKey2;

    MakeTransactionRef(tokenTransferTx);
    CWalletTx tokenttx(&wallet, MakeTransactionRef(tokenTransferTx));
    wallet.AddToWallet(tokenttx);
    BOOST_CHECK_EQUAL(wallet.IsMine(tokenTransferTx.vout[0]), ISMINE_NO);
    BOOST_CHECK_EQUAL(wallet.IsMine(tokenTransferTx.vout[1]), ISMINE_NO);

    wallet.AddKeyPubKey(key1, pubkey1);
    BOOST_CHECK_EQUAL(wallet.IsMine(tokenTransferTx.vout[0]), ISMINE_SPENDABLE);

    wallet.AddKeyPubKey(key2, pubkey2);
    BOOST_CHECK_EQUAL(wallet.IsMine(tokenTransferTx.vout[1]), ISMINE_SPENDABLE);
}

void broadcastAndProcessBlock(TestChainSetup* setup, const CTransactionRef tx)
{
    CValidationState state;
    bool pfMissingInputs;
    {
        LOCK(cs_main);
        AcceptToMemoryPool(mempool, state, tx, &pfMissingInputs, nullptr, true, 0);
    }
    setup->CreateAndProcessBlock({ CMutableTransaction(*tx) },
                                 CScript() <<  ToByteVector(setup->coinbaseKey.GetPubKey()) << OP_CHECKSIG);
}

void Sign(std::vector<unsigned char>& vchSig, CKey& signKey, const CScript& scriptPubKey, int inIndex, CMutableTransaction& outTx, int outIndex)
{
    uint256 hash = SignatureHash(scriptPubKey, outTx, inIndex, SIGHASH_ALL, outTx.vout[outIndex].nValue, SigVersion::BASE);
    signKey.Sign_Schnorr(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
}

BOOST_FIXTURE_TEST_CASE(wallet_token_balance, TestChainSetup)
{
    initKeys();
    std::vector<unsigned char> vchPubKey0(pubkey0.begin(), pubkey0.end());
    std::vector<unsigned char> vchPubKey1(pubkey1.begin(), pubkey1.end());
    std::vector<unsigned char> vchPubKey2(pubkey2.begin(), pubkey2.end());

    std::unique_ptr<CWallet> wallet = MakeUnique<CWallet>("mock", WalletDatabase::CreateMock());
    bool firstRun;
    wallet->LoadWallet(firstRun);
    // LOCK(cs_main);
    // LOCK(wallet->cs_wallet);
    AddKey(*wallet, key0);
    AddKey(*wallet, key1);
    AddKey(*wallet, key2);
    WalletRescanReserver reserver(wallet.get());
    reserver.reserve();
    wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);

    CMutableTransaction coinbaseSpendTx;
    coinbaseSpendTx.nFeatures = 1;
    coinbaseSpendTx.vin.resize(1);
    coinbaseSpendTx.vout.resize(1);
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[2]->GetHashMalFix();
    coinbaseSpendTx.vin[0].prevout.n = 0;
    coinbaseSpendTx.vout[0].nValue = 100 * CENT;
    coinbaseSpendTx.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<unsigned char> vchSig;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[2]->vout[0].scriptPubKey, 0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    broadcastAndProcessBlock(this, MakeTransactionRef(coinbaseSpendTx));

    wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);
    ColorIdentifier defaultColorId;
    BOOST_CHECK_EQUAL(wallet->GetBalance().size(), 1);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[defaultColorId], 100 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, defaultColorId),  100 * CENT);

    //token issue TYPE=1
    ColorIdentifier colorId(ColorIdentifier(coinbaseSpendTx.vout[0].scriptPubKey));
    CScript scriptPubKey = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenIssueTx;

    //tokenIssueTx(from coinbaseSpendTx) - 100 tokens
    tokenIssueTx.nFeatures = 1;
    tokenIssueTx.vin.resize(1);
    tokenIssueTx.vout.resize(1);
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();
    tokenIssueTx.vin[0].prevout.n = 0;
    tokenIssueTx.vout[0].nValue = 100 * CENT;
    tokenIssueTx.vout[0].scriptPubKey = scriptPubKey;

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    broadcastAndProcessBlock(this, MakeTransactionRef(tokenIssueTx));

    wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);
    BOOST_CHECK_EQUAL(wallet->GetBalance().size(), 2);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[defaultColorId],  0);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[colorId],  100 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, defaultColorId),  0);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, colorId),  100 * CENT);

    //token transfer TYPE=1
    CScript scriptPubKey1 = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash1) << OP_EQUALVERIFY << OP_CHECKSIG;

    CScript scriptPubKey2 = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash2) << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction tokenTransferTx;

    //tokenTransferTx
    tokenTransferTx.nFeatures = 1;
    tokenTransferTx.vin.resize(2);
    tokenTransferTx.vout.resize(2);
    tokenTransferTx.vin[0].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenTransferTx.vin[0].prevout.n = 0;
    tokenTransferTx.vin[1].prevout.hashMalFix = m_coinbase_txns[3]->GetHashMalFix();
    tokenTransferTx.vin[1].prevout.n = 0;
    tokenTransferTx.vout[0].nValue = 50 * CENT;
    tokenTransferTx.vout[0].scriptPubKey = scriptPubKey1;
    tokenTransferTx.vout[1].nValue = 50 * CENT;
    tokenTransferTx.vout[1].scriptPubKey = scriptPubKey2;

    CMutableTransaction coinbaseIn2(*m_coinbase_txns[3]);
    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, 0, tokenTransferTx, 0);
    tokenTransferTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[3]->vout[0].scriptPubKey, 1, tokenTransferTx, 0);
    tokenTransferTx.vin[1].scriptSig = CScript() << vchSig;

    broadcastAndProcessBlock(this, MakeTransactionRef(tokenTransferTx));

    wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);
    BOOST_CHECK_EQUAL(wallet->GetBalance().size(), 2);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[defaultColorId], 0 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[colorId], 100 * CENT); //both scriptpubkey have same colorid
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, defaultColorId),  0 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, colorId),  100 * CENT);

    //reissue same tokens - create input
    coinbaseSpendTx.vin[0].prevout.hashMalFix = m_coinbase_txns[1]->GetHashMalFix();

    Sign(vchSig, coinbaseKey, m_coinbase_txns[1]->vout[0].scriptPubKey,0, coinbaseSpendTx, 0);
    coinbaseSpendTx.vin[0].scriptSig = CScript() << vchSig;

    broadcastAndProcessBlock(this, MakeTransactionRef(coinbaseSpendTx));

    wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);
    BOOST_CHECK_EQUAL(wallet->GetBalance().size(), 2);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[defaultColorId], 100 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[colorId], 100 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, defaultColorId),  100 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, colorId),  100 * CENT);

    //reissue same tokens
    tokenIssueTx.vin[0].prevout.hashMalFix = coinbaseSpendTx.GetHashMalFix();

    Sign(vchSig, key0, coinbaseSpendTx.vout[0].scriptPubKey, 0, tokenIssueTx, 0);
    tokenIssueTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey0;

    broadcastAndProcessBlock(this, MakeTransactionRef(tokenIssueTx));
    wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);
    BOOST_CHECK_EQUAL(wallet->GetBalance().size(), 2);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[defaultColorId], 0 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[colorId], 200 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, defaultColorId),  0);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, colorId),  200 * CENT);

    CMutableTransaction tokenAggregateTx;
    //tokenAggregateTx - 1. no fee
    tokenAggregateTx.nFeatures = 1;
    tokenAggregateTx.vin.resize(4);
    tokenAggregateTx.vout.resize(1);
    tokenAggregateTx.vin[0].prevout.hashMalFix = tokenTransferTx.GetHashMalFix();
    tokenAggregateTx.vin[0].prevout.n = 0;
    tokenAggregateTx.vin[1].prevout.hashMalFix = tokenTransferTx.GetHashMalFix();
    tokenAggregateTx.vin[1].prevout.n = 1;
    tokenAggregateTx.vin[2].prevout.hashMalFix = tokenIssueTx.GetHashMalFix();
    tokenAggregateTx.vin[2].prevout.n = 0;
    tokenAggregateTx.vin[3].prevout.hashMalFix = m_coinbase_txns[4]->GetHashMalFix();
    tokenAggregateTx.vin[3].prevout.n = 0;
    tokenAggregateTx.vout[0].nValue = 200 * CENT;
    tokenAggregateTx.vout[0].scriptPubKey = scriptPubKey1;

    CMutableTransaction coinbaseIn3(*m_coinbase_txns[4]);
    Sign(vchSig, key1, tokenTransferTx.vout[0].scriptPubKey, 0, tokenAggregateTx, 0);
    tokenAggregateTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    Sign(vchSig, key2, tokenTransferTx.vout[1].scriptPubKey, 1, tokenAggregateTx, 0);
    tokenAggregateTx.vin[1].scriptSig = CScript() << vchSig << vchPubKey2;
    Sign(vchSig, key0, tokenIssueTx.vout[0].scriptPubKey, 2, tokenAggregateTx, 0);
    tokenAggregateTx.vin[2].scriptSig = CScript() << vchSig << vchPubKey0;
    Sign(vchSig, coinbaseKey, m_coinbase_txns[4]->vout[0].scriptPubKey, 3, tokenAggregateTx, 0);
    tokenAggregateTx.vin[3].scriptSig = CScript() << vchSig;

    broadcastAndProcessBlock(this, MakeTransactionRef(tokenAggregateTx));
    wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);
    BOOST_CHECK_EQUAL(wallet->GetBalance().size(), 2);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[defaultColorId], 0 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[colorId], 200 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, defaultColorId),  0);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, colorId),  200 * CENT);

    CMutableTransaction tokenBurnTx;

    //tokenBurnTx
    tokenBurnTx.nFeatures = 1;
    tokenBurnTx.vin.resize(2);
    tokenBurnTx.vout.resize(1);
    tokenBurnTx.vin[0].prevout.hashMalFix = tokenAggregateTx.GetHashMalFix();
    tokenBurnTx.vin[0].prevout.n = 0;
    tokenBurnTx.vin[1].prevout.hashMalFix = m_coinbase_txns[5]->GetHashMalFix();
    tokenBurnTx.vin[1].prevout.n = 0;
    tokenBurnTx.vout[0].nValue = 40 * CENT;
    tokenBurnTx.vout[0].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    Sign(vchSig, key1, tokenAggregateTx.vout[0].scriptPubKey, 0, tokenBurnTx, 0);
    tokenBurnTx.vin[0].scriptSig = CScript() << vchSig << vchPubKey1;
    CMutableTransaction coinbaseIn5(*m_coinbase_txns[5]);
    Sign(vchSig, coinbaseKey, m_coinbase_txns[5]->vout[0].scriptPubKey, 1, tokenBurnTx, 0);
    tokenBurnTx.vin[1].scriptSig = CScript() << vchSig;

    broadcastAndProcessBlock(this, MakeTransactionRef(tokenBurnTx));

    wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);
    BOOST_CHECK_EQUAL(wallet->GetBalance().size(), 1);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[defaultColorId], 40 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetBalance()[colorId], 0 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, defaultColorId),  40 * CENT);
    BOOST_CHECK_EQUAL(wallet->GetLegacyBalance(ISMINE_SPENDABLE, 0, nullptr, colorId),  0 * CENT);
}

BOOST_FIXTURE_TEST_CASE(wallet_tx_getdebit_and_getcredit, TestChainSetup)
{
    initKeys();
    LOCK(cs_main);

    // Prepare a dummy wallet that has all the coinbase transaction coins from genesis.
    CWallet wallet("dummy", WalletDatabase::CreateDummy());
    CWallet *pwallet = &wallet;
    AddKey(wallet, coinbaseKey);
    AddKey(wallet, key0);
    WalletRescanReserver reserver(&wallet);
    reserver.reserve();
    wallet.ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);

    ColorIdentifier defaultColorId;
    ColorIdentifier colorId(ColorIdentifier(m_coinbase_txns[0]->vout[0].scriptPubKey));

    // Check the initial balance state.
    BOOST_CHECK_EQUAL(wallet.GetBalance()[defaultColorId], 250 * COIN);
    BOOST_CHECK_EQUAL(wallet.GetBalance()[colorId], 0);

    // Create Token issue transaction
    CMutableTransaction tx;
    tx.nFeatures = 1;
    tx.vin.resize(1);
    tx.vout.resize(2);
    tx.vin[0].prevout.hashMalFix = m_coinbase_txns[0]->GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vout[0].nValue = 100 * CENT;
    tx.vout[0].scriptPubKey = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;;
    tx.vout[1].nValue = m_coinbase_txns[0]->vout[0].nValue;
    tx.vout[1].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    CWalletTx wtx(pwallet, MakeTransactionRef(tx));
    wallet.AddToWallet(wtx);

    BOOST_CHECK_EQUAL(wtx.GetDebit(ISMINE_SPENDABLE, defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx.GetDebit(ISMINE_SPENDABLE, colorId), 0);
    BOOST_CHECK_EQUAL(wtx.GetCredit(ISMINE_SPENDABLE, defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx.GetCredit(ISMINE_SPENDABLE, colorId), 100 * CENT);

    // Create a tx that has two debits, 50 TPC and 100 cent colored coin.
    CMutableTransaction tx2;
    tx2.nFeatures = 1;
    tx2.vin.resize(2);
    tx2.vout.resize(2);
    tx2.vin[0].prevout.hashMalFix = tx.GetHashMalFix();
    tx2.vin[0].prevout.n = 0;
    tx2.vin[1].prevout.hashMalFix = tx.GetHashMalFix();
    tx2.vin[1].prevout.n = 1;
    tx2.vout[0].nValue = 100 * CENT;
    tx2.vout[0].scriptPubKey = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;;
    tx2.vout[1].nValue = tx.vout[1].nValue;
    tx2.vout[1].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    CWalletTx wtx2(pwallet, MakeTransactionRef(tx2));
    wallet.AddToWallet(wtx2);

    BOOST_CHECK_EQUAL(wtx2.GetDebit(ISMINE_SPENDABLE, defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx2.GetDebit(ISMINE_SPENDABLE, colorId), 100 * CENT);
    BOOST_CHECK_EQUAL(wtx2.GetCredit(ISMINE_SPENDABLE, defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx2.GetCredit(ISMINE_SPENDABLE, colorId), 100 * CENT);
}

BOOST_FIXTURE_TEST_CASE(wallet_tx_getdebit_and_getcredit_with_watch_only, TestChainSetup)
{
    initKeys();
    LOCK(cs_main);

    // Prepare a dummy wallet that has all the coinbase transaction coins from genesis.
    CWallet wallet("dummy", WalletDatabase::CreateDummy());
    CWallet *pwallet = &wallet;
    AddKey(wallet, coinbaseKey);
    WalletRescanReserver reserver(&wallet);
    reserver.reserve();
    wallet.ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);

    ColorIdentifier defaultColorId;
    ColorIdentifier colorId(ColorIdentifier(m_coinbase_txns[0]->vout[0].scriptPubKey));

    // Check the initial balance state.
    BOOST_CHECK_EQUAL(wallet.GetBalance()[defaultColorId], 250 * COIN);
    BOOST_CHECK_EQUAL(wallet.GetBalance()[colorId], 0);

    auto colored_custom_script = CScript() << colorId.toVector() << OP_COLOR << OP_9 << OP_ADD << OP_11 << OP_EQUAL;
    auto uncolored_custom_script = CScript() << OP_9 << OP_ADD << OP_11 << OP_EQUAL;

    {
        LOCK(wallet.cs_wallet);
        wallet.AddWatchOnly(colored_custom_script, 0);
        wallet.AddWatchOnly(uncolored_custom_script, 0);
    }

    // Create Token issue transaction
    CMutableTransaction tx;
    tx.nFeatures = 1;
    tx.vin.resize(1);
    tx.vout.resize(2);
    tx.vin[0].prevout.hashMalFix = m_coinbase_txns[0]->GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vout[0].nValue = 100 * CENT;
    tx.vout[0].scriptPubKey = colored_custom_script;
    tx.vout[1].nValue = m_coinbase_txns[0]->vout[0].nValue;
    tx.vout[1].scriptPubKey =  uncolored_custom_script;

    CWalletTx wtx(pwallet, MakeTransactionRef(tx));
    wallet.AddToWallet(wtx);

    BOOST_CHECK_EQUAL(wtx.GetDebit(ISMINE_SPENDABLE, defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx.GetDebit(ISMINE_WATCH_ONLY, defaultColorId), 0);
    BOOST_CHECK_EQUAL(wtx.GetDebit(ISMINE_WATCH_ONLY, colorId), 0);
    BOOST_CHECK_EQUAL(wtx.GetCredit(ISMINE_SPENDABLE, defaultColorId), 0);
    BOOST_CHECK_EQUAL(wtx.GetCredit(ISMINE_WATCH_ONLY, defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx.GetCredit(ISMINE_WATCH_ONLY, colorId), 100 * CENT);

    // Create a tx that has two debits, 50 TPC and 100 cent colored coin.
    CMutableTransaction tx2;
    tx2.nFeatures = 1;
    tx2.vin.resize(2);
    tx2.vout.resize(2);
    tx2.vin[0].prevout.hashMalFix = tx.GetHashMalFix();
    tx2.vin[0].prevout.n = 0;
    tx2.vin[1].prevout.hashMalFix = tx.GetHashMalFix();
    tx2.vin[1].prevout.n = 1;
    tx2.vout[0].nValue = 100 * CENT;
    tx2.vout[0].scriptPubKey = colored_custom_script;
    tx2.vout[1].nValue = tx.vout[1].nValue;
    tx2.vout[1].scriptPubKey = uncolored_custom_script;

    CWalletTx wtx2(pwallet, MakeTransactionRef(tx2));
    wallet.AddToWallet(wtx2);

    BOOST_CHECK_EQUAL(wtx2.GetDebit(ISMINE_WATCH_ONLY, defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx2.GetDebit(ISMINE_WATCH_ONLY, colorId), 100 * CENT);
    BOOST_CHECK_EQUAL(wtx2.GetCredit(ISMINE_WATCH_ONLY, defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx2.GetCredit(ISMINE_WATCH_ONLY, colorId), 100 * CENT);
}

BOOST_FIXTURE_TEST_CASE(wallet_tx_getchange, TestChainSetup)
{
    initKeys();
    LOCK(cs_main);

    // Prepare a dummy wallet that has all the coinbase transaction coins from genesis.
    CWallet wallet("dummy", WalletDatabase::CreateDummy());
    CWallet *pwallet = &wallet;
    AddKey(wallet, coinbaseKey);
    AddKey(wallet, key0);
    WalletRescanReserver reserver(&wallet);
    reserver.reserve();
    wallet.ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver);

    ColorIdentifier defaultColorId;
    ColorIdentifier colorId(ColorIdentifier(m_coinbase_txns[0]->vout[0].scriptPubKey));

    // Create Token issue transaction
    CMutableTransaction tx;
    tx.nFeatures = 1;
    tx.vin.resize(1);
    tx.vout.resize(2);
    tx.vin[0].prevout.hashMalFix = m_coinbase_txns[0]->GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vout[0].nValue = 100 * CENT;
    tx.vout[0].scriptPubKey = CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;;
    tx.vout[1].nValue = m_coinbase_txns[0]->vout[0].nValue;
    tx.vout[1].scriptPubKey =  CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash0) << OP_EQUALVERIFY << OP_CHECKSIG;

    CWalletTx wtx(pwallet, MakeTransactionRef(tx));
    wallet.AddToWallet(wtx);

    BOOST_CHECK_EQUAL(wtx.GetChange(defaultColorId), 50 * COIN);
    BOOST_CHECK_EQUAL(wtx.GetChange(colorId), 100 * CENT);
}

BOOST_AUTO_TEST_SUITE_END()
