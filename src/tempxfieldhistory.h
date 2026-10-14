// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_TEMPXFIELDHISTORY_H
#define TAPYRUS_TEMPXFIELDHISTORY_H

#include <xfieldhistorymap.h>

#include <shared_mutex>

/*
 * class CTempXFieldHistory lets us use a temporary map to help handle situations like
 * LoadBlockFromDisk and ProcessBlockHeaders where the global map will not be accurate.
 * (The blocks being processed in LoadBlockFromDisk are not confirmed and not in the active chain.
 * If there is an aggregatePubKey change in one of the blocks, proof cannot be verified
 * for the rest of the blocks using the global CXFieldHistoryMap.)
 * This temp map used to store the xfield change encountered during processing
 * until the method completes. When the block is confirmed after AcceptBlock the xfield change
 * is added to the global list in CXFieldHistory.
 * IT uses RAII idiom and deallocates the memory when the object goes out of scope.
 */
class CTempXFieldHistory : public CXFieldHistoryMap{

    XFieldHistoryMapType* xfieldHistoryTemp;
public:
    //constructor to initialize the temp map
    inline explicit CTempXFieldHistory():CXFieldHistoryMap(true){
        xfieldHistoryTemp = new XFieldHistoryMapType;
        std::shared_lock<std::shared_mutex> lock(xfieldHistoryMutex);
        for(const auto& item : xfieldHistory)
            xfieldHistoryTemp->insert(item);
    }

    virtual ~CTempXFieldHistory(){
        delete xfieldHistoryTemp;
        xfieldHistoryTemp = nullptr;
    }

    XFieldHistoryMapType& getXFieldHistoryMap() const override {
        return *xfieldHistoryTemp;
    }

};

#endif // TAPYRUS_TEMPXFIELDHISTORY_H
