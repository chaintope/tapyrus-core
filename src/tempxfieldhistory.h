// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_TEMPXFIELDHISTORY_H
#define TAPYRUS_TEMPXFIELDHISTORY_H

#include <xfieldhistorymap.h>

/*
 * class CTempXFieldHistory is a copy of a history, for situations like
 * LoadBlockFromDisk and ProcessBlockHeaders where the active chain's history will not be accurate.
 * (The blocks being processed in LoadBlockFromDisk are not confirmed and not in the active chain.
 * If there is an aggregatePubKey change in one of the blocks, proof cannot be verified
 * for the rest of the blocks using the active chain's history.)
 * The copy stores the xfield changes encountered during processing
 * until the method completes. When the block is confirmed after AcceptBlock the xfield change
 * is added to the active chain's CXFieldHistory.
 */
class CTempXFieldHistory : public CXFieldHistoryMap{

public:
    inline explicit CTempXFieldHistory(const CXFieldHistoryMap& history) : CXFieldHistoryMap(history) {}
};

#endif // TAPYRUS_TEMPXFIELDHISTORY_H
