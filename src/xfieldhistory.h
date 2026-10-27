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
 * class CXFieldHistory holds the xfield changes of the active chain.
 * The node's instance is owned by ChainstateManager; tapyrus-genesis creates its own.
 */
class CXFieldHistory : public CXFieldHistoryMap{

public:
    CXFieldHistory() = default;
    CXFieldHistory(const CXFieldHistory&) = delete;
    CXFieldHistory& operator=(const CXFieldHistory&) = delete;

    inline explicit CXFieldHistory(const CBlock& genesis) {
        Init(genesis);
    }

    // Adds the genesis block's entries; entries already present are kept.
    void Init(const CBlock& genesis);

    void ToUniValue(TAPYRUS_XFIELDTYPES type, UniValue* xfieldChanges);
};

class IsXFieldLastInHistoryVisitor
{
    const CXFieldHistoryMap* history;
    uint32_t parentHeight;
public:
    IsXFieldLastInHistoryVisitor(const CXFieldHistoryMap* historyIn, uint32_t height)
        : history(historyIn), parentHeight(height) {}

    template <typename T>
    bool operator()(const T &xField) const {
        assert(history);
        TAPYRUS_XFIELDTYPES X = GetXFieldTypeFrom(xField);
        XFieldChange entry = history->Get(X, parentHeight);
        return std::get<T>(entry.xfieldValue) == T(xField);
    }

};

bool IsXFieldNew(const CXField& xfield, const CXFieldHistoryMap* pxfieldHistory, uint32_t parentHeight);

#endif // TAPYRUS_XFIELDHISTORY_H
