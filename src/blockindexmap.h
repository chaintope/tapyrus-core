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

#include <cassert>
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

class CBlockFileInfo;
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
    const BlockMap& Entries() const EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return m_entries; }

    CBlockIndex* LookupBlockIndex(const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Adds a header unless it is known; returns its entry. */
    CBlockIndex* AddToBlockIndex(const CBlockHeader& block) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Marks pindex to be written to the block tree database at the next flush. */
    void MarkDirty(CBlockIndex* pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main) { m_dirty.insert(pindex); }
    /** Writes the dirty entries, with the given block file info, in one synced batch; then forgets them. */
    bool WriteDirty(const std::vector<std::pair<int, const CBlockFileInfo*>>& files, int last_file) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Records that child has its transactions but waits for those of parent or an ancestor. */
    void AddUnlinked(CBlockIndex* parent, CBlockIndex* child) EXCLUSIVE_LOCKS_REQUIRED(cs_main) { m_unlinked.emplace(parent, child); }
    /** Removes and returns the children waiting for parent. */
    std::vector<CBlockIndex*> TakeUnlinkedChildren(CBlockIndex* parent) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Stops pindex waiting for its parent; pruning calls it once the block's data is gone. */
    void RemoveUnlinked(const CBlockIndex* pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Whether pindex waits for its parent. */
    bool IsUnlinked(const CBlockIndex* pindex) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** How many blocks wait for a parent. */
    size_t UnlinkedCount() const EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return m_unlinked.size(); }

    /** Opens the block tree database, replacing one already open; wipe empties it first. */
    void OpenDB(size_t cache_size, bool in_memory, bool wipe) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Closes the block tree database. */
    void CloseDB() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** The block tree database; open between OpenDB and CloseDB. */
    CBlockTreeDB& DB() const EXCLUSIVE_LOCKS_REQUIRED(cs_main) { assert(m_db); return *m_db; }

    /**
     * Loads the index from the block tree database and links it: chain transaction counts,
     * unlinked blocks, failed children and skip pointers.
     * @param[out] vSortedByHeight every entry, lowest height first
     */
    bool LoadBlockIndex(std::vector<CBlockIndex*>& vSortedByHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Deletes every entry and forgets the unlinked and dirty ones; the database stays open. */
    void Unload() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    /** Create a new block index entry for a given block hash */
    CBlockIndex* InsertBlockIndex(const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! Test only, defined by the tests that use it: adds an empty entry for hash, with no header.
    friend CBlockIndex* InsertBlockIndexForTest(BlockIndexMap& block_index, const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    BlockMap m_entries GUARDED_BY(cs_main);
    /** All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data.
     */
    std::multimap<CBlockIndex*, CBlockIndex*> m_unlinked GUARDED_BY(cs_main);
    /** Entries changed since the last write to the database. */
    std::set<CBlockIndex*> m_dirty GUARDED_BY(cs_main);
    std::unique_ptr<CBlockTreeDB> m_db GUARDED_BY(cs_main);
};

#endif // TAPYRUS_BLOCKINDEXMAP_H
