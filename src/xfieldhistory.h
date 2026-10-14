// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_XFIELDHISTORY_H
#define TAPYRUS_XFIELDHISTORY_H

#include <consensus/consensus.h>
#include <primitives/block.h>
#include <primitives/xfield.h>
#include <primitives/xfieldvalues.h>
#include <xfieldhistorymap.h>

#include <cassert>

class UniValue;

/*
 * class CXFieldHistory is an interface to access the global map of xfield changes
 * in the active chain which is guaranteed to be initialized.
 * this defines ots own constructor and other utility methods that act on the data in CXFieldHistoryMap
 */
class CXFieldHistory : public CXFieldHistoryMap{

public:
    CXFieldHistory():CXFieldHistoryMap(false) {}
    virtual ~CXFieldHistory(){}

    //constructor to initialize the confirmed global map
    inline explicit CXFieldHistory(const CBlock& genesis):CXFieldHistoryMap(false) {
        xfieldHistory.emplace(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChangeListWrapper(XFieldAggPubKey::BLOCKTREE_DB_KEY));
        xfieldHistory.emplace(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChangeListWrapper(XFieldMaxBlockSize::BLOCKTREE_DB_KEY));

        Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(genesis.xfield.xfieldValue, 0, genesis.GetHash()));
        Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(MAX_BLOCK_SIZE, 0, genesis.GetHash()));
    }

    XFieldHistoryMapType& getXFieldHistoryMap() const override {
        return xfieldHistory;
    }

    void ToUniValue(TAPYRUS_XFIELDTYPES type, UniValue* xfieldChanges);
};

class IsXFieldLastInHistoryVisitor
{
    CXFieldHistoryMap* history;
    uint32_t parentHeight;
public:
    IsXFieldLastInHistoryVisitor(CXFieldHistoryMap* historyIn, uint32_t height)
        : history(historyIn), parentHeight(height) {}

    template <typename T>
    bool operator()(const T &xField) const {
        assert(history);
        TAPYRUS_XFIELDTYPES X = GetXFieldTypeFrom(xField);
        XFieldChange entry = history->Get(X, parentHeight);
        return std::get<T>(entry.xfieldValue) == T(xField);
    }

};

bool IsXFieldNew(const CXField& xfield, CXFieldHistoryMap* pxfieldHistory, uint32_t parentHeight);

/** 
 * The maximum block size according the current xfield history */
uint32_t GetCurrentMaxBlockSize();

/** 
 * The maximum allowed number of signature check operations in a block (network rule) */
uint32_t GetMaxBlockSigops();

/** The maximum number of sigops we're willing to relay/mine in a single tx */
uint32_t GetMaxStandardTxSigops();

#endif // TAPYRUS_XFIELDHISTORY_H
