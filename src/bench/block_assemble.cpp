// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <chainparams.h>
#include <cachesettings.h>
#include <chainstatemanager.h>
#include <coinsviewcache.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <consensussettings.h>
#include <genesisfile.h>
#include <mempoolaccept.h>
#include <miner.h>
#include <notificationhub.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <scheduler.h>
#include <txdb.h>
#include <txmempool.h>
#include <utiltime.h>
#include <validation.h>
#include <validationinterface.h>
#include <tapyrusmodes.h>
#include <key_io.h>
#include <keystore.h>
#include <script/sign.h>
#include <script/signaturecreator.h>
#include <script/signaturedata.h>
#include <xfieldhistory.h>

#include <list>
#include <vector>
#include <array>
#include <thread>

static std::shared_ptr<CBlock> PrepareBlock(const CTxMemPool& mempool, const CScript& coinbase_scriptPubKey)
{
    std::shared_ptr<CBlock> block;
    {
        // Held across both, so the template builds on the tip the options read.
        LOCK(cs_main);
        const BlockAssembler::Options options(*g_policy_settings, gArgs, g_chainstate_manager);
        block = std::make_shared<CBlock>(BlockAssembler(g_chainstate_manager, mempool, options).CreateNewBlock(coinbase_scriptPubKey)->block);
    }

    block->nTime = g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);
    block->hashImMerkleRoot = BlockMerkleRoot(*block, nullptr, true);

    std::vector<unsigned char> proof;
    uint256 blockHash = block->GetHashForSign();

    CKey privKey = DecodeSecret(SIGN_BLOCK_PRIVKEY);
    assert(privKey.IsValid());
    privKey.Sign_Schnorr(blockHash, proof);

    XFieldAggPubKey aggpubkeyChange;
    g_chainstate_manager.XFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);

    block->AbsorbBlockProof(proof, aggpubkeyChange.getPubKey());

    return block;
}


static CTxIn MineBlock(const CTxMemPool& mempool, const CScript& coinbase_scriptPubKey)
{
    auto block = PrepareBlock(mempool, coinbase_scriptPubKey);

    bool processed{ProcessNewBlock(block, true, nullptr)};
    assert(processed);

    return CTxIn{block->vtx[0]->GetHashMalFix(), 0};
}


static void AssembleBlock(benchmark::State& state)
{
    // Switch to dev so we can mine faster
    writeTestGenesisBlockToFile(GetDataDir(false));
    SelectParams(TAPYRUS_OP_MODE::DEV);
    SelectFederationParams(TAPYRUS_OP_MODE::DEV);
    g_chainstate_manager.InitConsensusSettings(std::make_unique<const ConsensusSettings>(ReadGenesisBlock(), TAPYRUS_OP_MODE::DEV, FederationParams().NetworkId()));
    g_chainstate_manager.InitXFieldHistory(g_chainstate_manager.Settings().GenesisBlock());
    // No script-check worker threads are started, so -par=1 checks scripts on the connecting thread.
    gArgs.ForceSetArg("-par", "1");
    g_chainstate_manager.InitOptions(ChainstateManager::Options(gArgs, /*defaultConsistencyChecks=*/false, /*coinCacheUsage=*/size_t{5000} * 300));
    // Static: the global block files and coins views keep referring to it after this returns.
    static NotificationHub notifications;
    g_chainstate_manager.SetNotifications(&notifications);
    g_chainstate_manager.InitBlockFileManager(BlockFileManager::Options(gArgs, Params().PruneAfterHeight()), notifications);
    BlockIndexMap& blockIndex = g_chainstate_manager.BlockIndex();
    CBlockPolicyEstimator fee_estimator;
    CTxMemPool mempool;
    g_chainstate_manager.InitializeChainstate(mempool);

    const CKey privKey(DecodeSecret(SIGN_BLOCK_PRIVKEY));
    const CPubKey pubkey(privKey.GetPubKey());
    const CKeyID keyId(pubkey.GetID());
    const CScript SCRIPT_PUB { CScript() << OP_DUP << OP_HASH160 << ToByteVector(keyId) << OP_EQUALVERIFY << OP_CHECKSIG};
    CBasicKeyStore tempKeystore;
    tempKeystore.AddKey(privKey);

    {
        LOCK(cs_main);
        g_chainstate_manager.ValidationCaches().Resize(CacheSettings(gArgs).nValidationCacheBytes);
    }

    CScheduler scheduler;
    std::thread thread(std::bind(&CScheduler::serviceQueue, &scheduler));
    {
        {
            LOCK(cs_main);
            blockIndex.pblocktreedb.reset(new CBlockTreeDB(1 << 20, true));
            Chainstate& chainstate = g_chainstate_manager.ActiveChainstate();
            chainstate.InitCoinsDB(g_chainstate_manager.IssuedColorIds(), 1 << 23, true, false);
            chainstate.InitCoinsCache();
        }

        GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);
        GetMainSignals().RegisterWithMempoolSignals(mempool);
        RegisterValidationInterface(&fee_estimator);

        LoadGenesisBlock();
        CValidationState state;
        ActivateBestChain(state);
        assert(g_chainstate_manager.ActiveChain().Tip() != nullptr);
    }

    // Collect some loose transactions that spend the coinbases of our mined blocks
    constexpr size_t NUM_BLOCKS{200};
    std::array<CTransactionRef, NUM_BLOCKS> txs;
    for (size_t b{0}; b < NUM_BLOCKS; ++b) {
        CMutableTransaction tx;
        tx.vin.push_back(MineBlock(mempool, SCRIPT_PUB));
        tx.vout.emplace_back(1337, SCRIPT_PUB);

        SignatureData sigdata;
        bool ret{ProduceSignature(tempKeystore, MutableTransactionSignatureCreator(&tx, 0, 1337, SIGHASH_ALL), SCRIPT_PUB, sigdata)};
        assert(ret);
        tx.vin[0].scriptSig = sigdata.scriptSig;
        txs.at(b) = MakeTransactionRef(tx);

    }
    {
        LOCK(::cs_main); // Required for ::AcceptToMemoryPool.

        for (const auto& txr : txs) {
            MempoolAcceptOptions options;
            options.bypass_limits = true;
            MempoolAcceptResult result;
            bool ret{g_chainstate_manager.ActiveChainstate().AcceptToMemoryPool(txr, options, result)};
            assert(ret);
        }
    }

    while (state.KeepRunning()) {
        PrepareBlock(mempool, SCRIPT_PUB);
    }

    scheduler.stop();
    if(thread.joinable())
        thread.join();
    GetMainSignals().FlushBackgroundCallbacks();
    UnregisterValidationInterface(&fee_estimator);
    GetMainSignals().UnregisterWithMempoolSignals(mempool);
    GetMainSignals().UnregisterBackgroundSignalScheduler();
}

BENCHMARK(AssembleBlock, 700);
