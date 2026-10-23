// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/xfield.h>
#include <primitives/xfieldvalues.h>
#include <xfieldhistory.h>
#include <xfieldhistorymap.h>
#include <univalue.h>
#include <shared_mutex>

CXFieldHistoryMap::CXFieldHistoryMap(const CXFieldHistoryMap& other) {
    std::shared_lock<std::shared_mutex> lock(other.xfieldHistoryMutex);
    xfieldHistory = other.xfieldHistory;
}

XFieldChangeListWrapper& CXFieldHistoryMap::FindWithin(TAPYRUS_XFIELDTYPES type) {
    auto it = xfieldHistory.find(type);
    assert(it != xfieldHistory.end());
    return it->second;
}

const XFieldChangeListWrapper& CXFieldHistoryMap::FindWithin(TAPYRUS_XFIELDTYPES type) const {
    auto it = xfieldHistory.find(type);
    assert(it != xfieldHistory.end());
    return it->second;
}

bool CXFieldHistoryMap::Remove(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange) {
    std::unique_lock<std::shared_mutex> lock(xfieldHistoryMutex);
    auto& list = FindWithin(type);
    if (list.size() > 1 && list.back() == xFieldChange) {
        list.xfieldChanges.pop_back();
        return true;
    }
    return false;
}

void CXFieldHistoryMap::Add(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange) {
    std::unique_lock<std::shared_mutex> lock(xfieldHistoryMutex);

    // Check if new
    auto& listofXfieldChanges = FindWithin(type);
    for(const auto& xfieldItem : listofXfieldChanges)
        if( xfieldItem == xFieldChange )
            return;

    listofXfieldChanges.push_back(xFieldChange);
}

XFieldChangeList CXFieldHistoryMap::GetListCopy(TAPYRUS_XFIELDTYPES type) const {
    std::shared_lock<std::shared_mutex> lock(xfieldHistoryMutex);
    auto& listofXfieldChanges = FindWithin(type);
    return listofXfieldChanges.xfieldChanges;
}

char CXFieldHistoryMap::DBKey(TAPYRUS_XFIELDTYPES type) const {
    std::shared_lock<std::shared_mutex> lock(xfieldHistoryMutex);
    return FindWithin(type).key;
}

XFieldChange CXFieldHistoryMap::Get(TAPYRUS_XFIELDTYPES type, uint32_t height) const {
    std::shared_lock<std::shared_mutex> lock(xfieldHistoryMutex);
    auto& listofXfieldChanges = FindWithin(type);

    if(height == 0 || listofXfieldChanges.size() == 1)
        return listofXfieldChanges[0];

    if(height > listofXfieldChanges.back().height)
        return listofXfieldChanges.back();

    // Scan from the end so that when multiple entries share the same height
    // (e.g. an orphan entry followed by the active-chain entry after reorg),
    // the most recently added (active-chain) entry is returned first.
    for(int i = (int)listofXfieldChanges.size() - 1; i >= 0; i--) {
        if(listofXfieldChanges.at(i).height <= height)
            return listofXfieldChanges.at(i);
    }
    return listofXfieldChanges[0];
}


void CXFieldHistory::Init(const CBlock& genesis) {
    {
        std::unique_lock<std::shared_mutex> lock(xfieldHistoryMutex);
        xfieldHistory.emplace(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChangeListWrapper(XFieldAggPubKey::BLOCKTREE_DB_KEY));
        xfieldHistory.emplace(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChangeListWrapper(XFieldMaxBlockSize::BLOCKTREE_DB_KEY));
    }
    Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(genesis.xfield.xfieldValue, 0, genesis.GetHash()));
    Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(MAX_BLOCK_SIZE, 0, genesis.GetHash()));
}

void CXFieldHistory::ToUniValue(TAPYRUS_XFIELDTYPES type, UniValue* xFieldChangeUnival) {
    *xFieldChangeUnival = UniValue(UniValue::VARR);
    XFieldChangeList xFieldChangeList = this->GetListCopy(type);
    for (const auto& xFieldChange : xFieldChangeList)
    {
        UniValue xFieldChangeObj(UniValue::VOBJ);
        std::string value = XFieldDataToString(xFieldChange.xfieldValue);
        xFieldChangeObj.pushKV(value, xFieldChange.height);
        xFieldChangeUnival->push_back(xFieldChangeObj);
    }
}

int32_t CXFieldHistoryMap::GetReorgHeight() const
{
    std::shared_lock<std::shared_mutex> lock(xfieldHistoryMutex);
    std::vector<uint32_t> changeHeights;
    for(auto x : XFIELDTYPES_INIT_LIST)
    {
        changeHeights.push_back(FindWithin(x).back().height);
    }
    return *std::max_element(changeHeights.begin(), changeHeights.end());
}

bool IsXFieldNew(const CXField& xfield, const CXFieldHistoryMap* pxfieldHistory, uint32_t parentHeight)
{
    IsXFieldLastInHistoryVisitor checkVisitor(pxfieldHistory, parentHeight);
    return xfield.IsValid()
          && std::find(XFIELDTYPES_INIT_LIST.begin(), XFIELDTYPES_INIT_LIST.end(), xfield.xfieldType) != XFIELDTYPES_INIT_LIST.end()
          && !std::visit(checkVisitor, xfield.xfieldValue);
}

uint32_t CXFieldHistoryMap::GetCurrentMaxBlockSize() const {
    XFieldMaxBlockSize xfieldMaxBlockSize;
    GetLatest(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, xfieldMaxBlockSize);
    return xfieldMaxBlockSize.data;
}

uint32_t CXFieldHistoryMap::GetMaxBlockSigops() const {
    XFieldMaxBlockSize xfieldMaxBlockSize;
    GetLatest(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, xfieldMaxBlockSize);
    return xfieldMaxBlockSize.GetMaxBlockSigops();
}
