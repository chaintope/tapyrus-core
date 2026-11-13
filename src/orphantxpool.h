// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_ORPHANTXPOOL_H
#define TAPYRUS_ORPHANTXPOOL_H

#include <net.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <cstdint>
#include <map>
#include <optional>
#include <set>

class CBlock;

/** An orphan transaction and the peer that sent it. */
struct COrphanTx {
    CTransactionRef tx;
    NodeId fromPeer;
    int64_t nTimeExpire;
};

/**
 * Transactions whose inputs are not known yet, kept until their parents
 * arrive, as Core's TxOrphanage. It takes no lock: its owner synchronises
 * every call.
 */
class OrphanTxPool
{
public:
    /** Stores an orphan from peer; false if it is already stored or too large. */
    bool AddOrphanTx(const CTransactionRef& tx, NodeId peer);
    /** Removes an orphan; returns how many were removed, 0 or 1. */
    int EraseOrphanTx(uint256 hash);
    /** Removes every orphan peer sent. */
    void EraseOrphansFor(NodeId peer);
    /** Expires old orphans, then evicts random ones down to nMaxOrphans; returns how many were evicted. */
    unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);
    /** Removes the orphans that a connected block includes or conflicts with. */
    void EraseForBlock(const CBlock& block);

    bool HaveTx(const uint256& hash) const;
    /** The orphan with this hash, if it is stored. */
    std::optional<COrphanTx> GetTx(const uint256& hash) const;
    /** Adds to work_set every orphan that spends an output of tx. */
    void AddChildrenToWorkSet(const CTransaction& tx, std::set<uint256>& work_set) const;

protected:
    struct IteratorComparator
    {
        template<typename I>
        bool operator()(const I& a, const I& b) const
        {
            return &(*a) < &(*b);
        }
    };

    std::map<uint256, COrphanTx> mapOrphanTransactions;
    std::map<COutPoint, std::set<std::map<uint256, COrphanTx>::iterator, IteratorComparator>> mapOrphanTransactionsByPrev;
    /** When LimitOrphanTxSize next sweeps out expired orphans. */
    int64_t m_next_sweep{0};
};

#endif // TAPYRUS_ORPHANTXPOOL_H
