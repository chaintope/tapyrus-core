// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/nodecontextlookupimpl.h>

#include <chainstate.h>
#include <chainstatemanager.h>
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
    return m_node.fee_estimator->estimateSmartFee(num_blocks, calc, conservative);
}

unsigned int NodeContextLookupImpl::estimateMaxBlocks() const
{
    return m_node.fee_estimator->HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
}

CFeeRate NodeContextLookupImpl::mempoolMinFee() const
{
    return m_node.mempool->GetMinFee();
}

bool NodeContextLookupImpl::hasDescendantsInMempool(const uint256& txid) const
{
    LOCK(m_node.mempool->cs);
    auto it = m_node.mempool->mapTx.find(txid);
    return it != m_node.mempool->mapTx.end() && it->GetCountWithDescendants() > 1;
}

void NodeContextLookupImpl::getTransactionAncestry(const uint256& txid, size_t& ancestors, size_t& descendants) const
{
    m_node.mempool->GetTransactionAncestry(txid, ancestors, descendants);
}

bool NodeContextLookupImpl::checkChainLimits(const CTransactionRef& tx) const
{
    LockPoints lp;
    CTxMemPoolEntry entry(tx, 0, 0, 0, false, 0, lp);
    CTxMemPool::setEntries ancestors;
    std::string error;
    LOCK(m_node.mempool->cs);
    return m_node.mempool->CalculateMemPoolAncestors(entry, ancestors, g_policy_settings->ancestorLimit, g_policy_settings->ancestorSizeLimit,
                                               g_policy_settings->descendantLimit, g_policy_settings->descendantSizeLimit, error);
}

RBFTransactionState NodeContextLookupImpl::isRBFOptIn(const CTransaction& tx) const
{
    LOCK(m_node.mempool->cs);
    return IsRBFOptIn(tx, *m_node.mempool);
}

bool NodeContextLookupImpl::broadcastTransaction(const CTransactionRef& tx, CAmount max_tx_fee, bool relay, CValidationState& state)
{
    LOCK(::cs_main);
    if (!m_node.mempool->exists(tx->GetHashMalFix())) {
        CTxMempoolAcceptanceOptions opt;
        opt.nAbsurdFee = max_tx_fee;
        if (!g_chainstate_manager.ActiveChainstate().AcceptToMemoryPool(tx, opt)) {
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
