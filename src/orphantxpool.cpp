// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <orphantxpool.h>

#include <policy/policysettings.h>
#include <primitives/block.h>
#include <random.h>
#include <serialize.h>
#include <util.h>
#include <utiltime.h>
#include <version.h>

#include <algorithm>
#include <cassert>
#include <vector>

bool OrphanTxPool::AddOrphanTx(const CTransactionRef& tx, NodeId peer)
{
    const uint256& hash = tx->GetHashMalFix();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // -maxorphantx orphans of at most -maxorphantxsize bytes each, by default
    // 100 x 100,000 bytes = 10 megabytes, plus the byprev index:
    unsigned int sz = GetSerializeSize(*tx, SER_NETWORK, PROTOCOL_VERSION);
    if (sz > g_policy_settings->MaxOrphanTxSize())
    {
        LogPrint(BCLog::MEMPOOL, "ignoring large orphan tx (size: %u, hash: %s)\n", sz, hash.ToString());
        return false;
    }

    auto ret = mapOrphanTransactions.emplace(hash, COrphanTx{tx, peer, GetTime() + g_policy_settings->orphanExpiry});
    assert(ret.second);
    for (const CTxIn& txin : tx->vin) {
        mapOrphanTransactionsByPrev[txin.prevout].insert(ret.first);
    }

    LogPrint(BCLog::MEMPOOL, "stored orphan tx %s (mapsz %u outsz %u)\n", hash.ToString(),
             mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size());
    return true;
}

int OrphanTxPool::EraseOrphanTx(uint256 hash)
{
    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return 0;
    for (const CTxIn& txin : it->second.tx->vin)
    {
        auto itPrev = mapOrphanTransactionsByPrev.find(txin.prevout);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(it);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }
    mapOrphanTransactions.erase(it);
    return 1;
}

void OrphanTxPool::EraseOrphansFor(NodeId peer)
{
    int nErased = 0;
    std::map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end())
    {
        std::map<uint256, COrphanTx>::iterator maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer)
        {
            nErased += EraseOrphanTx(maybeErase->second.tx->GetHashMalFix());
        }
    }
    if (nErased > 0) LogPrint(BCLog::MEMPOOL, "Erased %d orphan tx from peer=%d\n", nErased, peer);
}

unsigned int OrphanTxPool::LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    unsigned int nEvicted = 0;
    int64_t nNow = GetTime();
    if (m_next_sweep <= nNow) {
        // Sweep out expired orphan pool entries:
        int nErased = 0;
        int64_t nMinExpTime = nNow + g_policy_settings->orphanExpiry - ORPHAN_TX_EXPIRE_INTERVAL;
        std::map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
        while (iter != mapOrphanTransactions.end())
        {
            std::map<uint256, COrphanTx>::iterator maybeErase = iter++;
            if (maybeErase->second.nTimeExpire <= nNow) {
                nErased += EraseOrphanTx(maybeErase->second.tx->GetHashMalFix());
            } else {
                nMinExpTime = std::min(maybeErase->second.nTimeExpire, nMinExpTime);
            }
        }
        // Sweep again 5 minutes after the next entry that expires in order to batch the linear scan.
        m_next_sweep = nMinExpTime + ORPHAN_TX_EXPIRE_INTERVAL;
        if (nErased > 0) LogPrint(BCLog::MEMPOOL, "Erased %d orphan tx due to expiration\n", nErased);
    }
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

void OrphanTxPool::EraseForBlock(const CBlock& block)
{
    std::vector<uint256> vOrphanErase;

    for (const CTransactionRef& ptx : block.vtx) {
        const CTransaction& tx = *ptx;

        // Which orphan pool entries must we evict?
        for (const auto& txin : tx.vin) {
            auto itByPrev = mapOrphanTransactionsByPrev.find(txin.prevout);
            if (itByPrev == mapOrphanTransactionsByPrev.end()) continue;
            for (auto mi = itByPrev->second.begin(); mi != itByPrev->second.end(); ++mi) {
                const CTransaction& orphanTx = *(*mi)->second.tx;
                const uint256& orphanHash = orphanTx.GetHashMalFix();
                vOrphanErase.push_back(orphanHash);
            }
        }
    }

    // Erase orphan transactions included or precluded by this block
    if (vOrphanErase.size()) {
        int nErased = 0;
        for (uint256 &orphanHash : vOrphanErase) {
            nErased += EraseOrphanTx(orphanHash);
        }
        LogPrint(BCLog::MEMPOOL, "Erased %d orphan tx included or conflicted by block\n", nErased);
    }
}

bool OrphanTxPool::HaveTx(const uint256& hash) const
{
    return mapOrphanTransactions.count(hash) > 0;
}

std::optional<COrphanTx> OrphanTxPool::GetTx(const uint256& hash) const
{
    auto it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end()) return std::nullopt;
    return it->second;
}

void OrphanTxPool::AddChildrenToWorkSet(const CTransaction& tx, std::set<uint256>& work_set) const
{
    const uint256& hash = tx.GetHashMalFix();
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        auto it_by_prev = mapOrphanTransactionsByPrev.find(COutPoint(hash, i));
        if (it_by_prev != mapOrphanTransactionsByPrev.end()) {
            for (const auto& elem : it_by_prev->second) {
                work_set.insert(elem->first);
            }
        }
    }
}
