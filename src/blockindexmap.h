// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_BLOCKINDEXMAP_H
#define TAPYRUS_BLOCKINDEXMAP_H

#include <cs_main.h>
#include <hasher.h>
#include <sync.h>
#include <uint256.h>

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

class CBlockHeader;
class CBlockIndex;
class CBlockTreeDB;

typedef std::unordered_map<uint256, CBlockIndex*, BlockHasher> BlockMap;

/**
 * The block index: every block header we know of, in memory and in the block
 * tree database, as the index part of Bitcoin Core's BlockManager. It changes
 * with the chain, so it is guarded by cs_main.
 */
class BlockIndexMap
{
public:
    BlockIndexMap();
    ~BlockIndexMap();

    /** Every block header we know of, by hash. */
    BlockMap mapBlockIndex GUARDED_BY(cs_main);
    /** All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data.
     */
    std::multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked GUARDED_BY(cs_main);
    /** Dirty block index entries. */
    std::set<CBlockIndex*> setDirtyBlockIndex GUARDED_BY(cs_main);
    /** The block index on disk. */
    std::unique_ptr<CBlockTreeDB> pblocktreedb GUARDED_BY(cs_main);

    CBlockIndex* LookupBlockIndex(const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Adds a header unless it is known; returns its entry. */
    CBlockIndex* AddToBlockIndex(const CBlockHeader& block) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Loads the index from pblocktreedb and links it: chain transaction counts,
     * unlinked blocks, failed children and skip pointers.
     * @param[out] vSortedByHeight every entry, lowest height first
     */
    bool LoadBlockIndex(std::vector<CBlockIndex*>& vSortedByHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Deletes every entry and forgets the unlinked and dirty ones; pblocktreedb is kept. */
    void Unload() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    /** Create a new block index entry for a given block hash */
    CBlockIndex* InsertBlockIndex(const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
};

#endif // TAPYRUS_BLOCKINDEXMAP_H
