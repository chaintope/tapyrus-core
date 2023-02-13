// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <bench/bench_genesis.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <miner.h>
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
#include <script/sigcache.h>
#include <xfieldhistory.h>

#include <boost/thread.hpp>

#include <list>
#include <vector>

static std::shared_ptr<CBlock> PrepareBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = std::make_shared<CBlock>(
        BlockAssembler{Params()}
            .CreateNewBlock(coinbase_scriptPubKey)
            ->block);

    block->nTime = ::chainActive.Tip()->GetMedianTimePast() + 1;
    block->hashMerkleRoot = BlockMerkleRoot(*block);
    block->hashImMerkleRoot = BlockMerkleRoot(*block, nullptr, true);

    std::vector<unsigned char> proof;
    uint256 blockHash = block->GetHashForSign();

    CKey privKey = DecodeSecret(SIGN_BLOCK_PRIVKEY);
    assert(privKey.IsValid());
    privKey.Sign_Schnorr(blockHash, proof);

    XFieldAggPubKey aggpubkeyChange;
    CXFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);

    block->AbsorbBlockProof(proof, aggpubkeyChange.getPubKey());

    return block;
}


static CTxIn MineBlock(const CScript& coinbase_scriptPubKey)
{
    auto block = PrepareBlock(coinbase_scriptPubKey);

    bool processed{ProcessNewBlock(block, true, nullptr)};
    assert(processed);

    return CTxIn{block->vtx[0]->GetHashMalFix(), 0};
}


static void AssembleBlock(benchmark::State& state)
{
    // Switch to dev so we can mine faster
    // Also segwit is active, so we can include witness transactions
    writeTestGenesisBlockToFile(GetDataDir(false));
    SelectParams(TAPYRUS_OP_MODE::DEV);
    SelectFederationParams(TAPYRUS_OP_MODE::DEV);
    CXFieldHistory xFieldHistory(FederationParams().GenesisBlock());

    const CKey privKey(DecodeSecret(SIGN_BLOCK_PRIVKEY));
    const CPubKey pubkey(privKey.GetPubKey());
    const CKeyID keyId(pubkey.GetID());
    const CScript SCRIPT_PUB { CScript() << OP_DUP << OP_HASH160 << ToByteVector(keyId) << OP_EQUALVERIFY << OP_CHECKSIG};
    CBasicKeyStore tempKeystore;
    tempKeystore.AddKey(privKey);

    InitSignatureCache();
    InitScriptExecutionCache();

    boost::thread_group thread_group;
    CScheduler scheduler;
    {
        ::pblocktree.reset(new CBlockTreeDB(1 << 20, true));
        ::pcoinsdbview.reset(new CCoinsViewDB(1 << 23, true));
        ::pcoinsTip.reset(new CCoinsViewCache(pcoinsdbview.get()));

        thread_group.create_thread(boost::bind(&CScheduler::serviceQueue, &scheduler));
        GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);
        LoadGenesisBlock();
        CValidationState state;
        ActivateBestChain(state);
        assert(::chainActive.Tip() != nullptr);
    }

    // Collect some loose transactions that spend the coinbases of our mined blocks
    constexpr size_t NUM_BLOCKS{200};
    std::array<CTransactionRef, NUM_BLOCKS> txs;
    for (size_t b{0}; b < NUM_BLOCKS; ++b) {
        CMutableTransaction tx;
        tx.vin.push_back(MineBlock(SCRIPT_PUB));
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
            CValidationState state;
            bool ret{::AcceptToMemoryPool(::mempool, state, txr, nullptr /* pfMissingInputs */, nullptr /* plTxnReplaced */, true /* bypass_limits */, /* nAbsurdFee */ 0)};
            assert(ret);
        }
    }

    while (state.KeepRunning()) {
        PrepareBlock(SCRIPT_PUB);
    }

    thread_group.interrupt_all();
    thread_group.join_all();
    GetMainSignals().FlushBackgroundCallbacks();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
}

BENCHMARK(AssembleBlock, 700);
