// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainstatemanager.h>
#include <consensus/validation.h>
#include <consensusconstants.h>
#include <interfaces/chain.h>
#include <policy/fees.h>
#include <policy/rbf.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/transactionsignatureserializer.h>
#include <test/test_tapyrus.h>
#include <txmempool.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

/** Spends the fixture's coinbases, paying back to the coinbase key so every output stays spendable. */
class ChainInterfaceSetup : public TestChainSetup
{
public:
    static constexpr CAmount FEE{10000};
    static constexpr CAmount MAX_TX_FEE{ConsensusConstants::COIN};

    std::unique_ptr<interfaces::Chain> m_chain{interfaces::MakeChain(m_node)};

    //! The active tip, read under cs_main.
    const CBlockIndex* Tip()
    {
        LOCK(cs_main);
        return g_chainstate_manager.ActiveChain().Tip();
    }

    //! Spends output 0 of parent, paying it less FEE back to the coinbase key.
    CTransactionRef Spend(const CTransactionRef& parent, uint32_t sequence = ConsensusConstants::SEQUENCE_FINAL)
    {
        const CScript script{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
        CMutableTransaction mtx;
        mtx.vin.emplace_back(COutPoint(parent->GetHashMalFix(), 0), CScript(), sequence);
        mtx.vout.emplace_back(parent->vout[0].nValue - FEE, script);
        std::vector<unsigned char> sig;
        const uint256 hash = SignatureHash(parent->vout[0].scriptPubKey, mtx, 0, SIGHASH_ALL, parent->vout[0].nValue);
        BOOST_REQUIRE(coinbaseKey.Sign_Schnorr(hash, sig));
        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        mtx.vin[0].scriptSig = CScript() << sig;
        return MakeTransactionRef(std::move(mtx));
    }

    //! Broadcasts tx without relaying, requiring the mempool to accept it.
    void Submit(const CTransactionRef& tx)
    {
        CValidationState state;
        BOOST_REQUIRE_MESSAGE(m_chain->broadcastTransaction(tx, MAX_TX_FEE, false, state), FormatStateMessage(state));
    }
};

BOOST_FIXTURE_TEST_SUITE(chain_interface_tests, ChainInterfaceSetup)

BOOST_AUTO_TEST_CASE(a_broadcast_transaction_enters_the_mempool)
{
    const CTransactionRef tx{Spend(m_coinbase_txns[0])};
    CValidationState state;
    BOOST_CHECK(m_chain->broadcastTransaction(tx, MAX_TX_FEE, true, state));
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK(m_node.mempool->exists(tx->GetHashMalFix()));
}

BOOST_AUTO_TEST_CASE(a_transaction_already_in_the_mempool_is_not_accepted_again)
{
    const CTransactionRef tx{Spend(m_coinbase_txns[0])};
    Submit(tx);
    const size_t size{m_node.mempool->size()};
    CValidationState state;
    BOOST_CHECK(m_chain->broadcastTransaction(tx, MAX_TX_FEE, true, state));
    BOOST_CHECK(state.IsValid());
    BOOST_CHECK_EQUAL(m_node.mempool->size(), size);
}

BOOST_AUTO_TEST_CASE(the_mempool_counts_ancestors_and_descendants)
{
    const CTransactionRef parent{Spend(m_coinbase_txns[0])};
    const CTransactionRef child{Spend(parent)};
    Submit(parent);
    Submit(child);

    BOOST_CHECK(m_chain->hasDescendantsInMempool(parent->GetHashMalFix()));
    size_t ancestors{0}, descendants{0};
    m_chain->getTransactionAncestry(parent->GetHashMalFix(), ancestors, descendants);
    BOOST_CHECK_EQUAL(ancestors, 1U);
    BOOST_CHECK_EQUAL(descendants, 2U);
    // Descendants are the most any of its ancestors has, so the child reports the parent's.
    m_chain->getTransactionAncestry(child->GetHashMalFix(), ancestors, descendants);
    BOOST_CHECK_EQUAL(ancestors, 2U);
    BOOST_CHECK_EQUAL(descendants, 2U);
}

BOOST_AUTO_TEST_CASE(a_transaction_within_the_chain_limits_passes)
{
    const CTransactionRef parent{Spend(m_coinbase_txns[0])};
    Submit(parent);
    BOOST_CHECK(m_chain->checkChainLimits(Spend(parent)));
}

BOOST_AUTO_TEST_CASE(signalling_and_mempool_final_transactions_report_their_rbf_state)
{
    BOOST_CHECK(m_chain->isRBFOptIn(*Spend(m_coinbase_txns[0], MAX_BIP125_RBF_SEQUENCE)) == RBFTransactionState::REPLACEABLE_BIP125);
    const CTransactionRef final_tx{Spend(m_coinbase_txns[1])};
    Submit(final_tx);
    BOOST_CHECK(m_chain->isRBFOptIn(*final_tx) == RBFTransactionState::FINAL);
}

BOOST_AUTO_TEST_CASE(the_fee_estimator_tracks_targets)
{
    BOOST_CHECK_GT(m_chain->estimateMaxBlocks(), 0U);
}

// Negative cases: what the chain interface must refuse or report as absent.

BOOST_AUTO_TEST_CASE(a_rejected_broadcast_reports_the_mempool_reason)
{
    const CTransactionRef tx{Spend(m_coinbase_txns[0])};
    CValidationState state;
    BOOST_CHECK(!m_chain->broadcastTransaction(tx, FEE - 1, true, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "absurdly-high-fee");
    BOOST_CHECK(!m_node.mempool->exists(tx->GetHashMalFix()));
}

BOOST_AUTO_TEST_CASE(a_transaction_outside_the_mempool_has_no_ancestry)
{
    const CTransactionRef parent{Spend(m_coinbase_txns[0])};
    const CTransactionRef child{Spend(parent)};
    Submit(parent);
    Submit(child);

    BOOST_CHECK(!m_chain->hasDescendantsInMempool(child->GetHashMalFix()));
    const CTransactionRef absent{Spend(m_coinbase_txns[1])};
    BOOST_CHECK(!m_chain->hasDescendantsInMempool(absent->GetHashMalFix()));
    size_t ancestors{1}, descendants{1};
    m_chain->getTransactionAncestry(absent->GetHashMalFix(), ancestors, descendants);
    BOOST_CHECK_EQUAL(ancestors, 0U);
    BOOST_CHECK_EQUAL(descendants, 0U);
}

BOOST_AUTO_TEST_CASE(a_transaction_past_the_ancestor_limit_fails_the_chain_limits)
{
    const CTransactionRef parent{Spend(m_coinbase_txns[0])};
    Submit(parent);
    const PolicySettingsOverride limit{{{"-limitancestorcount", "1"}}};
    BOOST_CHECK(!m_chain->checkChainLimits(Spend(parent)));
}

BOOST_AUTO_TEST_CASE(a_non_signalling_transaction_outside_the_mempool_has_unknown_rbf_state)
{
    BOOST_CHECK(m_chain->isRBFOptIn(*Spend(m_coinbase_txns[0])) == RBFTransactionState::UNKNOWN);
}

BOOST_AUTO_TEST_CASE(without_data_there_is_no_fee_estimate_or_mempool_minimum)
{
    BOOST_CHECK(m_chain->estimateSmartFee(2, false, nullptr) == CFeeRate(0));
    BOOST_CHECK(m_chain->mempoolMinFee() == CFeeRate(0));
}

BOOST_AUTO_TEST_CASE(a_synced_node_is_ready_to_broadcast)
{
    BOOST_REQUIRE(!IsInitialBlockDownload());
    BOOST_CHECK(m_chain->isReadyToBroadcast());
}

// Negative case: a node loading blocks must not rebroadcast.

BOOST_AUTO_TEST_CASE(a_node_importing_or_reindexing_is_not_ready_to_broadcast)
{
    g_chainstate_manager.GetBlockFileManager().SetImporting(true);
    BOOST_CHECK(!m_chain->isReadyToBroadcast());
    g_chainstate_manager.GetBlockFileManager().SetImporting(false);

    g_chainstate_manager.SetReindexing(true);
    BOOST_CHECK(!m_chain->isReadyToBroadcast());
    g_chainstate_manager.SetReindexing(false);
    BOOST_CHECK(m_chain->isReadyToBroadcast());
}

// The chain lookups, read against the fixture's active chain.

BOOST_AUTO_TEST_CASE(the_tip_and_heights_match_the_active_chain)
{
    LOCK(cs_main);
    const CChain& active = g_chainstate_manager.ActiveChain();
    BOOST_CHECK_EQUAL(*m_chain->getHeight(), active.Height());
    BOOST_CHECK_EQUAL(m_chain->getBlockHash(active.Height()), active.Tip()->GetBlockHash());
    BOOST_CHECK(m_chain->haveBlockOnDisk(active.Height()));
    BOOST_CHECK_EQUAL(*m_chain->findLocatorFork(m_chain->getTipLocator()), active.Height());
}

BOOST_AUTO_TEST_CASE(a_known_block_returns_what_was_asked_for)
{
    const CBlockIndex* tip = Tip();
    uint256 hash;
    int height{-1};
    int64_t time{0}, max_time{0}, mtp_time{0};
    bool in_active_chain{false};
    CBlock block;
    BOOST_CHECK(m_chain->findBlock(tip->GetBlockHash(), interfaces::FoundBlock().hash(hash).height(height).time(time).maxTime(max_time).mtpTime(mtp_time).inActiveChain(in_active_chain).data(block)));
    BOOST_CHECK_EQUAL(hash, tip->GetBlockHash());
    BOOST_CHECK_EQUAL(height, tip->nHeight);
    BOOST_CHECK_EQUAL(time, tip->GetBlockTime());
    BOOST_CHECK_EQUAL(max_time, tip->GetBlockTimeMax());
    BOOST_CHECK_EQUAL(mtp_time, tip->GetMedianTimePast());
    BOOST_CHECK(in_active_chain);
    BOOST_CHECK_EQUAL(block.GetHash(), tip->GetBlockHash());
}

BOOST_AUTO_TEST_CASE(ancestors_are_found_by_height_hash_and_common_descent)
{
    const CBlockIndex* tip = Tip();
    const CBlockIndex* lower = tip->GetAncestor(tip->nHeight - 3);
    uint256 hash;
    BOOST_CHECK(m_chain->findAncestorByHeight(tip->GetBlockHash(), lower->nHeight, interfaces::FoundBlock().hash(hash)));
    BOOST_CHECK_EQUAL(hash, lower->GetBlockHash());
    int height{-1};
    BOOST_CHECK(m_chain->findAncestorByHash(tip->GetBlockHash(), lower->GetBlockHash(), interfaces::FoundBlock().height(height)));
    BOOST_CHECK_EQUAL(height, lower->nHeight);
    uint256 ancestor;
    BOOST_CHECK(m_chain->findCommonAncestor(tip->GetBlockHash(), lower->GetBlockHash(), interfaces::FoundBlock().hash(ancestor)));
    BOOST_CHECK_EQUAL(ancestor, lower->GetBlockHash());
    BOOST_CHECK(m_chain->findFirstBlockWithTimeAndHeight(0, lower->nHeight, interfaces::FoundBlock().hash(hash)));
    BOOST_CHECK_EQUAL(hash, lower->GetBlockHash());
}

BOOST_AUTO_TEST_CASE(the_progress_size_and_key_come_from_the_chain)
{
    const uint256 tip = Tip()->GetBlockHash();
    const double progress{m_chain->guessVerificationProgress(tip)};
    BOOST_CHECK(progress > 0.0 && progress <= 1.0);
    BOOST_CHECK(!m_chain->havePruned());
    BOOST_CHECK(!m_chain->isPruneMode());
    BOOST_CHECK_EQUAL(m_chain->getMaxBlockSize(), g_chainstate_manager.XFieldHistory().GetCurrentMaxBlockSize());
    BOOST_CHECK(m_chain->getAggregatePubkey().IsFullyValid());
}

BOOST_AUTO_TEST_CASE(the_next_block_and_block_data_follow_the_active_chain)
{
    const CBlockIndex* tip = Tip();
    const CBlockIndex* prev = tip->pprev;
    uint256 next;
    bool reorg{true};
    BOOST_CHECK(m_chain->findNextBlock(prev->GetBlockHash(), prev->nHeight, interfaces::FoundBlock().hash(next), &reorg));
    BOOST_CHECK(!reorg);
    BOOST_CHECK_EQUAL(next, tip->GetBlockHash());
    BOOST_CHECK(m_chain->hasBlocks(tip->GetBlockHash(), 0, tip->nHeight));
    // Returns at once when the tip is the one given.
    m_chain->waitForNotificationsIfTipChanged(tip->GetBlockHash());
}

// Negative cases: what the lookups must report as not found.

BOOST_AUTO_TEST_CASE(the_tip_has_no_next_block_and_an_off_chain_block_reports_a_reorg)
{
    const CBlockIndex* tip = Tip();
    bool reorg{true};
    BOOST_CHECK(!m_chain->findNextBlock(tip->GetBlockHash(), tip->nHeight, {}, &reorg));
    BOOST_CHECK(!reorg);
    BOOST_CHECK(!m_chain->findNextBlock(uint256S("01"), tip->nHeight - 1, {}, &reorg));
    BOOST_CHECK(reorg);
    BOOST_CHECK(!m_chain->hasBlocks(uint256S("01")));
}

BOOST_AUTO_TEST_CASE(an_unknown_block_is_not_found_and_leaves_the_outputs)
{
    int height{-7};
    CBlock block;
    BOOST_CHECK(!m_chain->findBlock(uint256S("01"), interfaces::FoundBlock().height(height).data(block)));
    BOOST_CHECK_EQUAL(height, -7);
    BOOST_CHECK(!m_chain->findAncestorByHeight(uint256S("01"), 0));
    BOOST_CHECK(!m_chain->findCommonAncestor(uint256S("01"), uint256S("02")));
}

BOOST_AUTO_TEST_CASE(lookups_past_the_tip_or_off_the_ancestry_fail)
{
    const CBlockIndex* tip = Tip();
    BOOST_CHECK(!m_chain->haveBlockOnDisk(tip->nHeight + 1));
    BOOST_CHECK(!m_chain->findAncestorByHeight(tip->GetBlockHash(), tip->nHeight + 1));
    BOOST_CHECK(!m_chain->findFirstBlockWithTimeAndHeight(0, tip->nHeight + 1));
    // The tip does not descend from a later block.
    const CBlockIndex* lower = tip->GetAncestor(tip->nHeight - 3);
    BOOST_CHECK(!m_chain->findAncestorByHash(lower->GetBlockHash(), tip->GetBlockHash()));
}

BOOST_AUTO_TEST_SUITE_END()
