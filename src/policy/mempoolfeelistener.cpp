// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/mempoolfeelistener.h>

#include <mempoolentry.h>
#include <policy/fees.h>
#include <txmempool.h>
#include <uint256.h>

#include <vector>

MempoolFeeListener::MempoolFeeListener(CTxMemPool& mempool, CBlockPolicyEstimator& estimator)
    : m_entry_added(mempool.NotifyEntryAddedForFees.connect(
          [&estimator](const CTxMemPoolEntry& entry, bool validFeeEstimate) { estimator.processTransaction(entry, validFeeEstimate); })),
      m_entry_erased(mempool.NotifyEntryErasedForFees.connect(
          [&estimator](const uint256& txid) { estimator.removeTx(txid, false); })),
      m_block(mempool.NotifyBlockForFees.connect(
          [&estimator](unsigned int nBlockHeight, const std::vector<const CTxMemPoolEntry*>& entries) { estimator.processBlock(nBlockHeight, entries); }))
{
}
