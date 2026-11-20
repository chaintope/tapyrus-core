// Copyright (c) 2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <chainstatemanager.h>
#include <checkblock.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <consensussettings.h>
#include <miner.h>
#include <notificationhub.h>
#include <random.h>
#include <test/test_tapyrus.h>
#include <utiltime.h>
#include <validation.h>
#include <validationinterface.h>
#include <xfieldhistory.h>

#include <algorithm>
#include <string>
#include <vector>

namespace validation_block_tests {
/** Builds the test's blocks from its own mempool. */
struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(TAPYRUS_MODES::DEV) {}

    std::shared_ptr<CBlock> Block(const uint256& prev_hash, int height);
    std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock);
    const std::shared_ptr<const CBlock> GoodBlock(const uint256& prev_hash, const int height);
    const std::shared_ptr<const CBlock> BadBlock(const uint256& prev_hash, const int height);
    void BuildChain(const uint256& root, int height, const unsigned int invalid_rate, const unsigned int branch_rate, const unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks);
};
} // namespace validation_block_tests

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

std::shared_ptr<CBlock> RegtestingSetup::Block(const uint256& prev_hash, int height)
{
    static int i = 0;
    static uint64_t time = g_chainstate_manager.Settings().GenesisBlock().nTime;

    CScript pubKey;
    pubKey << i++ << OP_TRUE;

    std::unique_ptr<CBlockTemplate> ptemplate;
    {
        // Held across both, so the template builds on the tip the options read.
        LOCK(cs_main);
        const BlockAssembler::Options options(*g_policy_settings, gArgs, g_chainstate_manager);
        ptemplate = BlockAssembler(g_chainstate_manager, *m_node.mempool, options).CreateNewBlock(pubKey, false);
    }
    auto pblock = std::make_shared<CBlock>(ptemplate->block);
    pblock->hashPrevBlock = prev_hash;
    pblock->nTime = ++time;
    BOOST_CHECK_EQUAL(pblock->proof.size(), 0);

    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vout.resize(1);
    txCoinbase.vin[0].prevout.n = height;
    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));

    return pblock;
}

std::shared_ptr<CBlock> RegtestingSetup::FinalizeBlock(std::shared_ptr<CBlock> pblock)
{
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    pblock->hashImMerkleRoot = BlockMerkleRoot(*pblock, nullptr, true);

    XFieldAggPubKey aggpubkeyChange;
    g_chainstate_manager.XFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);
    CPubKey aggpubkey(aggpubkeyChange.getPubKey());
    std::vector<unsigned char> blockProof;
    createSignedBlockProof(*pblock, blockProof);
    pblock->AbsorbBlockProof(blockProof, aggpubkey);
    BOOST_CHECK_EQUAL(pblock->proof.size(), blockProof.size());
    return pblock;
}

// construct a valid block
const std::shared_ptr<const CBlock> RegtestingSetup::GoodBlock(const uint256& prev_hash, const int height)
{
    return FinalizeBlock(Block(prev_hash, height));
}

// construct an invalid block (but with a valid header)
const std::shared_ptr<const CBlock> RegtestingSetup::BadBlock(const uint256& prev_hash, const int height)
{
    auto pblock = Block(prev_hash, height);

    CMutableTransaction coinbase_spend;
    coinbase_spend.vin.push_back(CTxIn(COutPoint(pblock->vtx[0]->GetHash(), height), CScript(), 0));
    coinbase_spend.vout.push_back(pblock->vtx[0]->vout[0]);

    CTransactionRef tx = MakeTransactionRef(coinbase_spend);
    pblock->vtx.push_back(tx);

    auto ret = FinalizeBlock(pblock);
    return ret;
}

void RegtestingSetup::BuildChain(const uint256& root, int height, const unsigned int invalid_rate, const unsigned int branch_rate, const unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks)
{
    if (blocks.size() >= max_size) return;

    bool gen_invalid = GetRand(100) < invalid_rate;
    bool gen_fork = GetRand(100) < branch_rate;

    const std::shared_ptr<const CBlock> pblock = gen_invalid ? BadBlock(root, height) : GoodBlock(root, height);
    blocks.push_back(pblock);
    if (!gen_invalid) {
        BuildChain(pblock->GetHash(), height + 1, invalid_rate, branch_rate, max_size, blocks);
    }

    if (gen_fork) {
        blocks.push_back(GoodBlock(root, height));
        BuildChain(blocks.back()->GetHash(), height + 1, invalid_rate, branch_rate, max_size, blocks);
    }
}

//! Records the fork reports the chain makes.
class ForkReports : public Notifications
{
public:
    void ReportLargeWorkFork(const uint256& fork_base) override { m_reports.push_back("fork"); }
    void ReportInvalidChainAhead() override { m_reports.push_back("invalid chain"); }
    void ReportForkResolved() override { m_reports.push_back("resolved"); }

    size_t Count(const std::string& report) const { return std::count(m_reports.begin(), m_reports.end(), report); }
    const std::vector<std::string>& Reports() const { return m_reports; }

private:
    std::vector<std::string> m_reports;
};

//! Builds chains on genesis out of initial download, recording what the chain reports.
class ForkReportSetup : public RegtestingSetup
{
public:
    const std::string INVALID_CHAIN_WARNING{"Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade."};

    ForkReportSetup()
    {
        bool ignored;
        ProcessNewBlock(std::make_shared<CBlock>(g_chainstate_manager.Settings().GenesisBlock()), true, &ignored);
        m_node.notifications.Subscribe(m_reports);
    }
    ~ForkReportSetup()
    {
        m_node.notifications.Unsubscribe(m_reports);
        SetMockTime(0);
    }

    //! An invalid block on genesis with length - 1 valid blocks on it; the invalid one is processed last,
    //! so the whole chain is tried.
    void ProcessInvalidChain(int32_t length)
    {
        std::vector<std::shared_ptr<const CBlock>> blocks{BadBlock(g_chainstate_manager.Settings().GenesisBlock().GetHash(), 1)};
        for (int32_t height = 2; height <= length; ++height) blocks.push_back(GoodBlock(blocks.back()->GetHash(), height));
        Process(blocks, std::vector<std::shared_ptr<const CBlock>>(blocks.rbegin(), blocks.rend()));
    }

    //! length valid blocks on genesis.
    void ProcessValidChain(int32_t length)
    {
        std::vector<std::shared_ptr<const CBlock>> blocks{GoodBlock(g_chainstate_manager.Settings().GenesisBlock().GetHash(), 1)};
        for (int32_t height = 2; height <= length; ++height) blocks.push_back(GoodBlock(blocks.back()->GetHash(), height));
        Process(blocks, blocks);
    }

    ForkReports m_reports;

private:
    void Process(const std::vector<std::shared_ptr<const CBlock>>& blocks, const std::vector<std::shared_ptr<const CBlock>>& order)
    {
        // The chain reports forks only once the tip is recent.
        SetMockTime(blocks.back()->GetBlockTime());
        CValidationState state;
        std::vector<CBlockHeader> headers;
        for (const auto& block : blocks) headers.push_back(block->GetBlockHeader());
        BOOST_REQUIRE(ProcessNewBlockHeaders(headers, state));
        bool ignored;
        for (const auto& block : order) ProcessNewBlock(block, true, &ignored);
    }
};

BOOST_FIXTURE_TEST_CASE(an_invalid_chain_seven_blocks_ahead_is_reported, ForkReportSetup)
{
    ProcessInvalidChain(7);
    BOOST_CHECK_GE(m_reports.Count("invalid chain"), 1U);
    BOOST_CHECK_EQUAL(m_reports.Count("fork"), 0U);
    BOOST_CHECK_EQUAL(m_node.notifications.GetWarnings("statusbar"), INVALID_CHAIN_WARNING);
}

BOOST_FIXTURE_TEST_CASE(catching_up_with_an_invalid_chain_resolves_it, ForkReportSetup)
{
    ProcessInvalidChain(7);
    ProcessValidChain(1);
    BOOST_REQUIRE(!m_reports.Reports().empty());
    BOOST_CHECK_EQUAL(m_reports.Reports().back(), "resolved");
    BOOST_CHECK(m_node.notifications.GetWarnings("statusbar") != INVALID_CHAIN_WARNING);
}

// Negative cases: what the chain must not report.

BOOST_FIXTURE_TEST_CASE(an_invalid_chain_six_blocks_ahead_is_not_reported, ForkReportSetup)
{
    ProcessInvalidChain(6);
    BOOST_CHECK_EQUAL(m_reports.Count("invalid chain"), 0U);
    BOOST_CHECK_GE(m_reports.Count("resolved"), 1U);
    BOOST_CHECK(m_node.notifications.GetWarnings("statusbar") != INVALID_CHAIN_WARNING);
}

BOOST_FIXTURE_TEST_CASE(a_valid_chain_reports_no_fork, ForkReportSetup)
{
    ProcessValidChain(10);
    BOOST_CHECK_EQUAL(m_reports.Count("invalid chain"), 0U);
    BOOST_CHECK_EQUAL(m_reports.Count("fork"), 0U);
}

BOOST_AUTO_TEST_CASE(processnewblock_signals_ordering)
{
    // build a large-ish chain that's likely to have some forks
    std::vector<std::shared_ptr<const CBlock>> blocks;
    while (blocks.size() < 50) {
        blocks.clear();
        BuildChain(g_chainstate_manager.Settings().GenesisBlock().GetHash(), 1, 15, 10, 500, blocks);
    }

    bool ignored;
    CValidationState state;
    std::vector<CBlockHeader> headers;
    std::transform(blocks.begin(), blocks.end(), std::back_inserter(headers), [](std::shared_ptr<const CBlock> b) { return b->GetBlockHeader(); });

    // Process all the headers so we understand the toplogy of the chain
    BOOST_CHECK(ProcessNewBlockHeaders(headers, state));

    // Connect the genesis block and drain any outstanding events
    ProcessNewBlock(std::make_shared<CBlock>(g_chainstate_manager.Settings().GenesisBlock()), true, &ignored);
    SyncWithValidationInterfaceQueue();

    // subscribe to events (this subscriber will validate event ordering)
    const CBlockIndex* initial_tip = nullptr;
    {
        LOCK(cs_main);
        initial_tip = g_chainstate_manager.ActiveChain().Tip();
    }
    TestSubscriber sub(initial_tip->GetBlockHash());
    RegisterValidationInterface(&sub);

    // create a bunch of threads that repeatedly process a block generated above at random
    // this will create parallelism and randomness inside validation - the ValidationInterface
    // will subscribe to events generated during block validation and assert on ordering invariance
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&blocks]() {
            bool ignored;
            for (int i = 0; i < 1000; i++) {
                auto block = blocks[GetRand(blocks.size() - 1)];
                BOOST_CHECK_EQUAL(block->proof.size(), 64);
                CValidationState state;
                BOOST_CHECK(CheckBlockHeader(*block, state, g_chainstate_manager.RulesLatest()));
                ProcessNewBlock(block, true, &ignored);
            }

            // to make sure that eventually we process the full chain - do it here
            for (auto block : blocks) {
                BOOST_CHECK_EQUAL(block->proof.size(), 64);
                CValidationState state;
                BOOST_CHECK(CheckBlockHeader(*block, state, g_chainstate_manager.RulesLatest()));
                ProcessNewBlock(block, true, &ignored);
                if (block->vtx.size() == 1) {
                    bool processed = ProcessNewBlock(block, true, &ignored);
                    assert(processed);
                }
            }
        });
    }

    for (auto& thread: threads) {
        if (thread.joinable()) thread.join();
    }
    while (GetMainSignals().CallbacksPending() > 0) {
        MilliSleep(100);
    }

    UnregisterValidationInterface(&sub);

    BOOST_CHECK_EQUAL(sub.m_expected_tip, g_chainstate_manager.ActiveChain().Tip()->GetBlockHash());
    BOOST_CHECK(g_chainstate_manager.ActiveChain().Tip()->nHeight != 0);
}

BOOST_AUTO_TEST_SUITE_END()
