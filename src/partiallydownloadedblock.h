// Copyright (c) 2016-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_PARTIALLYDOWNLOADEDBLOCK_H
#define TAPYRUS_PARTIALLYDOWNLOADEDBLOCK_H

#include <blockencodings.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <utility>
#include <vector>

class CTxMemPool;

class PartiallyDownloadedBlock {
protected:
    std::vector<CTransactionRef> txn_available;
    size_t prefilled_count = 0, mempool_count = 0, extra_count = 0;
    CTxMemPool* pool;
public:
    CBlockHeader header;
    explicit PartiallyDownloadedBlock(CTxMemPool* poolIn) : pool(poolIn) {}

    // extra_txn is a list of extra transactions to look at, in <hash, reference> form
    ReadStatus InitData(const CBlockHeaderAndShortTxIDs& cmpctblock, const std::vector<std::pair<uint256, CTransactionRef>>& extra_txn);
    bool IsTxAvailable(size_t index) const;
    ReadStatus FillBlock(CBlock& block, const std::vector<CTransactionRef>& vtx_missing, int nHeight = -1);
};

#endif // TAPYRUS_PARTIALLYDOWNLOADEDBLOCK_H
