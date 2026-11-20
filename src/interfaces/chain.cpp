// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/chain.h>

#include <connman.h>
#include <consensus/validation.h>
#include <net_processing.h>
#include <nodecontext.h>
#include <policy/fees.h>
#include <policy/policysettings.h>
#include <policy/rbf.h>
#include <txmempool.h>
#include <utilmemory.h>
#include <validation.h>

namespace interfaces {
namespace {

class ChainImpl : public Chain
{
public:
    explicit ChainImpl(NodeContext& node) : m_node(node) {}

    CFeeRate estimateSmartFee(int num_blocks, bool conservative, FeeCalculation* calc) const override
    {
        return m_node.fee_estimator->estimateSmartFee(num_blocks, calc, conservative);
    }

    unsigned int estimateMaxBlocks() const override
    {
        return m_node.fee_estimator->HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    }

    CFeeRate mempoolMinFee() const override
    {
        return ::mempool.GetMinFee(g_policy_settings->maxMempool);
    }

    bool hasDescendantsInMempool(const uint256& txid) const override
    {
        LOCK(::mempool.cs);
        auto it = ::mempool.mapTx.find(txid);
        return it != ::mempool.mapTx.end() && it->GetCountWithDescendants() > 1;
    }

    void getTransactionAncestry(const uint256& txid, size_t& ancestors, size_t& descendants) const override
    {
        ::mempool.GetTransactionAncestry(txid, ancestors, descendants);
    }

    bool checkChainLimits(const CTransactionRef& tx) const override
    {
        LockPoints lp;
        CTxMemPoolEntry entry(tx, 0, 0, 0, false, 0, lp);
        CTxMemPool::setEntries ancestors;
        std::string error;
        LOCK(::mempool.cs);
        return ::mempool.CalculateMemPoolAncestors(entry, ancestors, g_policy_settings->ancestorLimit, g_policy_settings->ancestorSizeLimit,
                                                   g_policy_settings->descendantLimit, g_policy_settings->descendantSizeLimit, error);
    }

    RBFTransactionState isRBFOptIn(const CTransaction& tx) const override
    {
        LOCK(::mempool.cs);
        return IsRBFOptIn(tx, ::mempool);
    }

    bool broadcastTransaction(const CTransactionRef& tx, CAmount max_tx_fee, bool relay, CValidationState& state) override
    {
        LOCK(::cs_main);
        if (!::mempool.exists(tx->GetHashMalFix())) {
            CTxMempoolAcceptanceOptions opt;
            opt.nAbsurdFee = max_tx_fee;
            if (!AcceptToMemoryPool(tx, opt)) {
                state = opt.state;
                return false;
            }
        }
        if (relay && m_node.connman) {
            RelayTransaction(*tx, m_node.connman.get());
        }
        return true;
    }

    WalletNotifications& notifications() const override
    {
        return m_node.notifications;
    }

private:
    NodeContext& m_node;
};

} // namespace

std::unique_ptr<Chain> MakeChain(NodeContext& node) { return MakeUnique<ChainImpl>(node); }

} // namespace interfaces
