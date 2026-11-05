// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_INTERFACES_NODECONTEXTLOOKUPIMPL_H
#define TAPYRUS_INTERFACES_NODECONTEXTLOOKUPIMPL_H

#include <interfaces/nodecontextlookup.h>

struct NodeContext;

/** The node-side NodeContextLookup. The program's main function creates it
 *  beside the NodeContext it reads, which must outlive it. */
class NodeContextLookupImpl : public interfaces::NodeContextLookup
{
public:
    explicit NodeContextLookupImpl(NodeContext& node);

    CFeeRate estimateSmartFee(int num_blocks, bool conservative, FeeCalculation* calc) const override;
    unsigned int estimateMaxBlocks() const override;
    CFeeRate mempoolMinFee() const override;
    bool hasDescendantsInMempool(const uint256& txid) const override;
    void getTransactionAncestry(const uint256& txid, size_t& ancestors, size_t& descendants) const override;
    bool checkChainLimits(const CTransactionRef& tx) const override;
    RBFTransactionState isRBFOptIn(const CTransaction& tx) const override;
    bool broadcastTransaction(const CTransactionRef& tx, CAmount max_tx_fee, bool relay, CValidationState& state) override;

private:
    NodeContext& m_node;
};

#endif // TAPYRUS_INTERFACES_NODECONTEXTLOOKUPIMPL_H
