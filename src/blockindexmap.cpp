// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockindexmap.h>

#include <blockindex.h>
#include <primitives/block.h>
#include <txdb.h>

#include <algorithm>
#include <iterator>
#include <utility>

BlockIndexMap::BlockIndexMap() = default;

BlockIndexMap::~BlockIndexMap() = default;

CBlockIndex* BlockIndexMap::LookupBlockIndex(const uint256& hash) const
{
    AssertLockHeld(cs_main);
    BlockMap::const_iterator it = m_entries.find(hash);
    return it == m_entries.end() ? nullptr : it->second;
}

CBlockIndex* BlockIndexMap::AddToBlockIndex(const CBlockHeader& block)
{
    AssertLockHeld(cs_main);

    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = m_entries.find(hash);
    if (it != m_entries.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = m_entries.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = m_entries.find(block.hashPrevBlock);
    if (miPrev != m_entries.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
    }
    pindexNew->nTimeMax = (pindexNew->pprev ? std::max(pindexNew->pprev->nTimeMax, pindexNew->nTime) : pindexNew->nTime);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);

    m_dirty.insert(pindexNew);

    return pindexNew;
}

CBlockIndex* BlockIndexMap::InsertBlockIndex(const uint256& hash)
{
    AssertLockHeld(cs_main);

    if (hash.IsNull())
        return nullptr;

    // Return existing
    BlockMap::iterator mi = m_entries.find(hash);
    if (mi != m_entries.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    mi = m_entries.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool BlockIndexMap::LoadBlockIndex(std::vector<CBlockIndex*>& vSortedByHeight)
{
    if (!m_db->LoadBlockIndexGuts([this](const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return this->InsertBlockIndex(hash); }))
        return false;

    std::vector<std::pair<int, CBlockIndex*> > vHeightAndIndex;
    vHeightAndIndex.reserve(m_entries.size());
    for (const std::pair<const uint256, CBlockIndex*>& item : m_entries)
    {
        CBlockIndex* pindex = item.second;
        vHeightAndIndex.push_back(std::make_pair(pindex->nHeight, pindex));
    }
    sort(vHeightAndIndex.begin(), vHeightAndIndex.end());
    vSortedByHeight.clear();
    vSortedByHeight.reserve(vHeightAndIndex.size());
    for (const std::pair<int, CBlockIndex*>& item : vHeightAndIndex)
    {
        CBlockIndex* pindex = item.second;
        pindex->nTimeMax = (pindex->pprev ? std::max(pindex->pprev->nTimeMax, pindex->nTime) : pindex->nTime);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    m_unlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (!(pindex->nStatus & BLOCK_FAILED_MASK) && pindex->pprev && (pindex->pprev->nStatus & BLOCK_FAILED_MASK)) {
            pindex->nStatus |= BLOCK_FAILED_CHILD;
            m_dirty.insert(pindex);
        }
        if (pindex->pprev)
            pindex->BuildSkip();
        vSortedByHeight.push_back(pindex);
    }

    return true;
}

bool BlockIndexMap::WriteDirty(const std::vector<std::pair<int, const CBlockFileInfo*>>& files, int last_file)
{
    AssertLockHeld(cs_main);
    std::vector<const CBlockIndex*> blocks(m_dirty.begin(), m_dirty.end());
    m_dirty.clear();
    return m_db->WriteBatchSync(files, last_file, blocks);
}

std::vector<CBlockIndex*> BlockIndexMap::TakeUnlinkedChildren(CBlockIndex* parent)
{
    AssertLockHeld(cs_main);
    std::vector<CBlockIndex*> children;
    const auto range{m_unlinked.equal_range(parent)};
    for (auto it = range.first; it != range.second; ++it) children.push_back(it->second);
    m_unlinked.erase(range.first, range.second);
    return children;
}

void BlockIndexMap::RemoveUnlinked(const CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);
    const auto range{m_unlinked.equal_range(pindex->pprev)};
    for (auto it = range.first; it != range.second;) {
        it = it->second == pindex ? m_unlinked.erase(it) : std::next(it);
    }
}

bool BlockIndexMap::IsUnlinked(const CBlockIndex* pindex) const
{
    AssertLockHeld(cs_main);
    const auto range{m_unlinked.equal_range(pindex->pprev)};
    return std::any_of(range.first, range.second, [pindex](const auto& entry) { return entry.second == pindex; });
}

void BlockIndexMap::OpenDB(size_t cache_size, bool in_memory, bool wipe)
{
    AssertLockHeld(cs_main);
    // A new database deletes the existing files when wiping, which fails while they are still open.
    m_db.reset();
    m_db = std::make_unique<CBlockTreeDB>(cache_size, in_memory, wipe);
}

void BlockIndexMap::CloseDB()
{
    AssertLockHeld(cs_main);
    m_db.reset();
}

void BlockIndexMap::Unload()
{
    m_unlinked.clear();
    m_dirty.clear();
    for (BlockMap::value_type& entry : m_entries) {
        delete entry.second;
    }
    m_entries.clear();
}
