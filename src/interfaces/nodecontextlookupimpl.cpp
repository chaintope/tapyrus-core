// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/nodecontextlookupimpl.h>

#include <connman.h>
#include <consensus/validation.h>
#include <net_processing.h>
#include <nodecontext.h>
#include <policy/fees.h>
#include <policy/policysettings.h>
#include <policy/rbf.h>
#include <txmempool.h>
#include <validation.h>

NodeContextLookupImpl::NodeContextLookupImpl(NodeContext& node) : m_node(node) {}

CFeeRate NodeContextLookupImpl::estimateSmartFee(int num_blocks, bool conservative, FeeCalculation* calc) const
{
    return ::feeEstimator.estimateSmartFee(num_blocks, calc, conservative);
}

unsigned int NodeContextLookupImpl::estimateMaxBlocks() const
{
    return ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
}

CFeeRate NodeContextLookupImpl::mempoolMinFee() const
{
    return ::mempool.GetMinFee(g_policy_settings->maxMempool);
}

bool NodeContextLookupImpl::hasDescendantsInMempool(const uint256& txid) const
{
    LOCK(::mempool.cs);
    auto it = ::mempool.mapTx.find(txid);
    return it != ::mempool.mapTx.end() && it->GetCountWithDescendants() > 1;
}

void NodeContextLookupImpl::getTransactionAncestry(const uint256& txid, size_t& ancestors, size_t& descendants) const
{
    ::mempool.GetTransactionAncestry(txid, ancestors, descendants);
}

bool NodeContextLookupImpl::checkChainLimits(const CTransactionRef& tx) const
{
    LockPoints lp;
    CTxMemPoolEntry entry(tx, 0, 0, 0, false, 0, lp);
    CTxMemPool::setEntries ancestors;
    std::string error;
    LOCK(::mempool.cs);
    return ::mempool.CalculateMemPoolAncestors(entry, ancestors, g_policy_settings->ancestorLimit, g_policy_settings->ancestorSizeLimit,
                                               g_policy_settings->descendantLimit, g_policy_settings->descendantSizeLimit, error);
}

RBFTransactionState NodeContextLookupImpl::isRBFOptIn(const CTransaction& tx) const
{
    LOCK(::mempool.cs);
    return IsRBFOptIn(tx, ::mempool);
}

bool NodeContextLookupImpl::broadcastTransaction(const CTransactionRef& tx, CAmount max_tx_fee, bool relay, CValidationState& state)
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

WalletNotifications& NodeContextLookupImpl::notifications() const
{
    return m_node.notifications;
}
