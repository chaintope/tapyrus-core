// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <consensusconstants.h>
#include <interfaces/chain.h>
#include <policy/fees.h>
#include <policy/rbf.h>
#include <primitives/transaction.h>
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

BOOST_AUTO_TEST_SUITE_END()
