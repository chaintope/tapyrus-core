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
#include <utility>

BlockIndexMap::BlockIndexMap() = default;

BlockIndexMap::~BlockIndexMap() = default;

CBlockIndex* BlockIndexMap::LookupBlockIndex(const uint256& hash) const
{
    AssertLockHeld(cs_main);
    BlockMap::const_iterator it = mapBlockIndex.find(hash);
    return it == mapBlockIndex.end() ? nullptr : it->second;
}

CBlockIndex* BlockIndexMap::AddToBlockIndex(const CBlockHeader& block)
{
    AssertLockHeld(cs_main);

    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
    }
    pindexNew->nTimeMax = (pindexNew->pprev ? std::max(pindexNew->pprev->nTimeMax, pindexNew->nTime) : pindexNew->nTime);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);

    setDirtyBlockIndex.insert(pindexNew);

    return pindexNew;
}

CBlockIndex* BlockIndexMap::InsertBlockIndex(const uint256& hash)
{
    AssertLockHeld(cs_main);

    if (hash.IsNull())
        return nullptr;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool BlockIndexMap::LoadBlockIndex(std::vector<CBlockIndex*>& vSortedByHeight)
{
    if (!pblocktreedb->LoadBlockIndexGuts([this](const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return this->InsertBlockIndex(hash); }))
        return false;

    std::vector<std::pair<int, CBlockIndex*> > vHeightAndIndex;
    vHeightAndIndex.reserve(mapBlockIndex.size());
    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex)
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
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (!(pindex->nStatus & BLOCK_FAILED_MASK) && pindex->pprev && (pindex->pprev->nStatus & BLOCK_FAILED_MASK)) {
            pindex->nStatus |= BLOCK_FAILED_CHILD;
            setDirtyBlockIndex.insert(pindex);
        }
        if (pindex->pprev)
            pindex->BuildSkip();
        vSortedByHeight.push_back(pindex);
    }

    return true;
}

void BlockIndexMap::Unload()
{
    mapBlocksUnlinked.clear();
    setDirtyBlockIndex.clear();
    for (BlockMap::value_type& entry : mapBlockIndex) {
        delete entry.second;
    }
    mapBlockIndex.clear();
}
