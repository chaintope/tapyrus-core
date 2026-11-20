// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mempoolentry.h>

#include <core_memusage.h>
#include <policy/policy.h>
#include <serialize.h>
#include <version.h>

#include <cassert>

CTxMemPoolEntry::CTxMemPoolEntry(const CTransactionRef& _tx, const CAmount& _nFee,
                                 int64_t _nTime, unsigned int _entryHeight,
                                 bool _spendsCoinbase, int32_t _sigOpsCost, LockPoints lp):
    tx(_tx), nFee(_nFee), nTime(_nTime), entryHeight(_entryHeight),
    spendsCoinbase(_spendsCoinbase), sigOpCost(_sigOpsCost), lockPoints(lp)
{
    nTxSize = GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    nUsageSize = RecursiveDynamicUsage(tx);

    nCountWithDescendants = 1;
    nSizeWithDescendants = GetTxSize();
    nModFeesWithDescendants = nFee;

    feeDelta = 0;

    nCountWithAncestors = 1;
    nSizeWithAncestors = GetTxSize();
    nModFeesWithAncestors = nFee;
    nSigOpCostWithAncestors = sigOpCost;
}

void CTxMemPoolEntry::UpdateFeeDelta(int64_t newFeeDelta)
{
    nModFeesWithDescendants += newFeeDelta - feeDelta;
    nModFeesWithAncestors += newFeeDelta - feeDelta;
    feeDelta = newFeeDelta;
}

void CTxMemPoolEntry::UpdateLockPoints(const LockPoints& lp)
{
    lockPoints = lp;
}

int32_t CTxMemPoolEntry::GetTxSize() const
{
    return GetTransactionSize(nTxSize, sigOpCost);
}

void CTxMemPoolEntry::UpdateDescendantState(int32_t modifySize, CAmount modifyFee, int64_t modifyCount)
{
    nSizeWithDescendants += modifySize;
    assert(nSizeWithDescendants > 0);
    nModFeesWithDescendants += modifyFee;
    nCountWithDescendants += uint64_t(modifyCount);
    assert(int64_t(nCountWithDescendants) > 0);
}

void CTxMemPoolEntry::UpdateAncestorState(int32_t modifySize, CAmount modifyFee, int64_t modifyCount, int32_t modifySigOps)
{
    nSizeWithAncestors += modifySize;
    assert(nSizeWithAncestors > 0);
    nModFeesWithAncestors += modifyFee;
    nCountWithAncestors += uint64_t(modifyCount);
    assert(int64_t(nCountWithAncestors) > 0);
    nSigOpCostWithAncestors += modifySigOps;
    assert(int(nSigOpCostWithAncestors) >= 0);
}
