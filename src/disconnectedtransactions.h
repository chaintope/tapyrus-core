// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_DISCONNECTEDTRANSACTIONS_H
#define TAPYRUS_DISCONNECTEDTRANSACTIONS_H

#include <core_memusage.h>
#include <hasher.h>
#include <memusage.h>
#include <mempoolentry.h>
#include <primitives/transaction.h>

#include <cassert>
#include <cstdint>
#include <vector>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

/**
 * DisconnectedBlockTransactions

 * During the reorg, it's desirable to re-add previously confirmed transactions
 * to the mempool, so that anything not re-confirmed in the new chain is
 * available to be mined. However, it's more efficient to wait until the reorg
 * is complete and process all still-unconfirmed transactions at that time,
 * since we expect most confirmed transactions to (typically) still be
 * confirmed in the new chain, and re-accepting to the memory pool is expensive
 * (and therefore better to not do in the middle of reorg-processing).
 * Instead, store the disconnected transactions (in order!) as we go, remove any
 * that are included in blocks in the new chain, and then process the remaining
 * still-unconfirmed transactions at the end.
 */

// multi_index tag names
struct txid_index {};
struct insertion_order {};

struct DisconnectedBlockTransactions {
    typedef boost::multi_index_container<
        CTransactionRef,
        boost::multi_index::indexed_by<
            // sorted by txid
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<txid_index>,
                mempoolentry_Imtxid,
                SaltedTxidHasher
            >,
            // sorted by order in the blockchain
            boost::multi_index::sequenced<
                boost::multi_index::tag<insertion_order>
            >
        >
    > indexed_disconnected_transactions;

    // It's almost certainly a logic bug if we don't clear out queuedTx before
    // destruction, as we add to it while disconnecting blocks, and then we
    // need to re-process remaining transactions to ensure mempool consistency.
    // For now, assert() that we've emptied out this object on destruction.
    // This assert() can always be removed if the reorg-processing code were
    // to be refactored such that this assumption is no longer true (for
    // instance if there was some other way we cleaned up the mempool after a
    // reorg, besides draining this object).
    ~DisconnectedBlockTransactions() { assert(queuedTx.empty()); }

    indexed_disconnected_transactions queuedTx;
    uint64_t cachedInnerUsage = 0;

    // Estimate the overhead of queuedTx to be 6 pointers + an allocation, as
    // no exact formula for boost::multi_index_contained is implemented.
    size_t DynamicMemoryUsage() const {
        return memusage::MallocUsage(sizeof(CTransactionRef) + 6 * sizeof(void*)) * queuedTx.size() + cachedInnerUsage;
    }

    void addTransaction(const CTransactionRef& tx)
    {
        queuedTx.insert(tx);
        cachedInnerUsage += RecursiveDynamicUsage(tx);
    }

    // Remove entries based on txid_index, and update memory usage.
    void removeForBlock(const std::vector<CTransactionRef>& vtx)
    {
        // Short-circuit in the common case of a block being added to the tip
        if (queuedTx.empty()) {
            return;
        }
        for (auto const &tx : vtx) {
            auto it = queuedTx.find(tx->GetHashMalFix());
            if (it != queuedTx.end()) {
                cachedInnerUsage -= RecursiveDynamicUsage(*it);
                queuedTx.erase(it);
            }
        }
    }

    // Remove an entry by insertion_order index, and update memory usage.
    void removeEntry(indexed_disconnected_transactions::index<insertion_order>::type::iterator entry)
    {
        cachedInnerUsage -= RecursiveDynamicUsage(*entry);
        queuedTx.get<insertion_order>().erase(entry);
    }

    void clear()
    {
        cachedInnerUsage = 0;
        queuedTx.clear();
    }
};

#endif // TAPYRUS_DISCONNECTEDTRANSACTIONS_H
