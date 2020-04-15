// Copyright (c) 2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <miner.h>
#include <random.h>
#include <test/test_tapyrus.h>
#include <validation.h>
#include <validation.cpp>
#include <validationinterface.h>

struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(TAPYRUS_MODES::DEV) {}
};

BOOST_FIXTURE_TEST_SUITE(validation_block_tests, RegtestingSetup)

struct TestSubscriber : public CValidationInterface {
    uint256 m_expected_tip;

    TestSubscriber(uint256 tip) : m_expected_tip(tip) {}

    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, pindexNew->GetBlockHash());
    }

    void BlockConnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex, const std::vector<CTransactionRef>& txnConflicted) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->hashPrevBlock);
        BOOST_CHECK_EQUAL(m_expected_tip, pindex->pprev->GetBlockHash());

        m_expected_tip = block->GetHash();
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock>& block) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->GetHash());

        m_expected_tip = block->hashPrevBlock;
    }
};

int aggPubkeyIndex = 1;
CCriticalSection cs_list;
std::vector<aggPubkeyAndHeight> aggregatePubkeyHeightList;

std::shared_ptr<CBlock> Block(const uint256& prev_hash, int height)
{
    static int i = 0;
    static uint64_t time = FederationParams().GenesisBlock().nTime;
    static int federationBlock = 0;

    CScript pubKey;
    pubKey << i++ << OP_TRUE;

    auto ptemplate = BlockAssembler(Params()).CreateNewBlock(pubKey, false);
    auto pblock = std::make_shared<CBlock>(ptemplate->block);
    pblock->hashPrevBlock = prev_hash;
    pblock->nTime = ++time;
    if(++federationBlock % 50 == 0)
    {
        pblock->xType = 1;
        pblock->xValue = ParseHex(ValidPubKeyStrings[aggPubkeyIndex]);
        aggPubkeyIndex = ++aggPubkeyIndex % 15;

        aggPubkeyAndHeight pair;
        pair.aggpubkey = CPubKey(pblock->xValue.begin(), pblock->xValue.end());
        pair.height = height + 1;
        aggregatePubkeyHeightList.push_back(pair);
    }
    else
    {
        pblock->xType = 0;
        pblock->xValue.clear();
    }
    BOOST_CHECK_EQUAL(pblock->proof.size(), 0);

    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vout.resize(1);
    txCoinbase.vin[0].scriptWitness.SetNull();
    txCoinbase.vin[0].prevout.n = height;
    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));

    return pblock;
}

std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock)
{
    static CPubKey genesispubkey;
    genesispubkey.Set(validAggPubKey, validAggPubKey + 33);

    static CKey genesiskey;
    genesiskey.Set(validAggPrivateKey, validAggPrivateKey + 32, true);

    static std::vector<CKey> keys = getValidPrivateKeys(15);
    static auto pubkeys = validPubKeys(15);

    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    pblock->hashImMerkleRoot = BlockMerkleRoot(*pblock, nullptr, true);

    uint256 blockHash = pblock->GetHashForSign();
    std::vector<unsigned char> blockProof;

    if(FederationParams().GetLatestAggregatePubkey() == genesispubkey)
        genesiskey.Sign_Schnorr(blockHash, blockProof);
    else
        keys[aggPubkeyIndex-1].Sign_Schnorr(blockHash, blockProof);

    pblock->AbsorbBlockProof(blockProof, FederationParams().GetLatestAggregatePubkey());

    BOOST_CHECK_EQUAL(pblock->proof.size(), blockProof.size());
    return pblock;
}

// construct a valid block
const std::shared_ptr<const CBlock> GoodBlock(const uint256& prev_hash, const int height)
{
    return FinalizeBlock(Block(prev_hash, height));
}

// construct an invalid block. Alternate with a valid and invalid header
const std::shared_ptr<const CBlock> BadBlock(const uint256& prev_hash, const int height)
{
    static bool errInHeader = false;
    static uint8_t xType = 1;
    auto pblock = Block(prev_hash, height);
    if(errInHeader)
        pblock->xType = xType;
    xType = (xType == 1 ? 2 : 1); //test with values 1 & 2
    errInHeader = !errInHeader;

    CMutableTransaction coinbase_spend;
    coinbase_spend.vin.push_back(CTxIn(COutPoint(pblock->vtx[0]->GetHash(), height), CScript(), 0));
    coinbase_spend.vout.push_back(pblock->vtx[0]->vout[0]);

    CTransactionRef tx = MakeTransactionRef(coinbase_spend);
    pblock->vtx.push_back(tx);

    auto ret = FinalizeBlock(pblock);
    return ret;
}

void BuildChain(const uint256& root, int height, const unsigned int invalid_rate, const unsigned int branch_rate, const unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks)
{
    if (height >= max_size) return;

    bool gen_invalid = GetRand(100) < invalid_rate;
    bool gen_fork = GetRand(100) < branch_rate;

    const std::shared_ptr<const CBlock> pblock = gen_invalid ? BadBlock(root, height) : GoodBlock(root, height);
    blocks.push_back(pblock);
    if (!gen_invalid) {
        BuildChain(pblock->GetHash(), height + 1, invalid_rate, branch_rate, max_size, blocks);
    }

    /*if (gen_fork) {
        blocks.push_back(GoodBlock(root, height));
        BuildChain(blocks.back()->GetHash(), height + 1, invalid_rate, branch_rate, max_size, blocks);
    }*/
}

BOOST_AUTO_TEST_CASE(processnewblock_signals_ordering)
{
    // build a large-ish chain that's likely to have some forks
    std::vector<std::shared_ptr<const CBlock>> blocks;
    while (blocks.size() < 20) {
        blocks.clear();
        BuildChain(FederationParams().GenesisBlock().GetHash(), 1, 15, 10, 100, blocks);
    }

    bool ignored;
    CValidationState state;
    std::vector<CBlockHeader> headers;
    std::transform(blocks.begin(), blocks.end(), std::back_inserter(headers), [](std::shared_ptr<const CBlock> b) { return b->GetBlockHeader(); });

    // Process all the headers so we understand the toplogy of the chain
    BOOST_CHECK(ProcessNewBlockHeaders(headers, state));

    // Connect the genesis block and drain any outstanding events
    ProcessNewBlock(std::make_shared<CBlock>(FederationParams().GenesisBlock()), true, &ignored);
    SyncWithValidationInterfaceQueue();

    // subscribe to events (this subscriber will validate event ordering)
    const CBlockIndex* initial_tip = nullptr;
    {
        LOCK(cs_main);
        initial_tip = chainActive.Tip();
    }
    TestSubscriber sub(initial_tip->GetBlockHash());
    RegisterValidationInterface(&sub);

    // create a bunch of threads that repeatedly process a block generated above at random
    // this will create parallelism and randomness inside validation - the ValidationInterface
    // will subscribe to events generated during block validation and assert on ordering invariance
    boost::thread_group threads;
    for (int i = 0; i < 10; i++) {
        threads.create_thread([&blocks]() {
            bool ignored;
            for (int i = 0; i < 1000; i++) {
                auto block = blocks[GetRand(blocks.size() - 1)];
                BOOST_CHECK_EQUAL(block->proof.size(), 64);
                CValidationState state;
                BOOST_CHECK(CheckBlockHeader(*block, state, block->vtx[0]->vin[0].prevout.n));
                ProcessNewBlock(block, true, &ignored);
            }

            // to make sure that eventually we process the full chain - do it here
            for (auto block : blocks) {
                if (block->vtx.size() == 1) {
                    BOOST_CHECK_EQUAL(block->proof.size(), 64);
                    CValidationState state;
                    BOOST_CHECK(CheckBlockHeader(*block, state, block->vtx[0]->vin[0].prevout.n));
                    bool processed = ProcessNewBlock(block, true, &ignored);
                    BOOST_CHECK(processed);
                }
            }
        });
    }

    threads.join_all();
    while (GetMainSignals().CallbacksPending() > 0) {
        MilliSleep(100);
    }

    UnregisterValidationInterface(&sub);

    //verify that federation blocks were processed correctly.
    for ( auto& aggPubKeyHeightPair : aggregatePubkeyHeightList)
    {
        std::vector<unsigned char> aggpubkey(aggPubKeyHeightPair.aggpubkey.begin(), aggPubKeyHeightPair.aggpubkey.end());
        BOOST_CHECK_EQUAL(FederationParams().GetHeightFromAggregatePubkey(aggpubkey), aggPubKeyHeightPair.height);
    }

    BOOST_CHECK_EQUAL(sub.m_expected_tip, chainActive.Tip()->GetBlockHash());
}

BOOST_AUTO_TEST_SUITE_END()
