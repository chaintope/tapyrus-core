// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/xfield.h>
#include <xfieldhistory.h>
#include <txdb.h>
#include <univalue.h>

XFieldHistoryMapType CXFieldHistoryMap::xfieldHistory;

bool CXFieldHistoryMap::IsNew(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange) const
{
    auto& listofXfieldChanges = (isTemp ? this->getXFieldHistoryMap() : xfieldHistory).find(type)->second;

    for(const auto& xfieldItem : listofXfieldChanges)
        if( xfieldItem == xFieldChange )
            return false;
    return true;
}

void CXFieldHistoryMap::Add(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange) {
    if(!IsNew(type, xFieldChange))
        return;

    (isTemp ? this->getXFieldHistoryMap() : xfieldHistory).find(type)->second.push_back(xFieldChange);
}

const XFieldChange& CXFieldHistoryMap::Get(TAPYRUS_XFIELDTYPES type, uint32_t height) {

    auto& listofXfieldChanges = (isTemp ? this->getXFieldHistoryMap() : xfieldHistory).find(type)->second;

    if(height == 0 || listofXfieldChanges.size() == 1)
        return listofXfieldChanges[0]; 

    if(height < 0 || height > listofXfieldChanges.back().height)
        return listofXfieldChanges.back();

    for(unsigned int i = 0; i < listofXfieldChanges.size(); i++) {
        if(height == listofXfieldChanges.at(i).height || (listofXfieldChanges.at(i).height < height && height < listofXfieldChanges.at(i+1).height))
            return listofXfieldChanges.at(i);
    }
    return listofXfieldChanges.back();
}

const XFieldChange& CXFieldHistoryMap::Get(TAPYRUS_XFIELDTYPES type, uint256 blockHash) {

    auto& listofXfieldChanges = (isTemp ? this->getXFieldHistoryMap() : xfieldHistory).find(type)->second;
    //TODO: return the corrext xfield applicable to any block by checking the index.
    for(unsigned int i = 0; i < listofXfieldChanges.size(); i++) {
        if(blockHash == listofXfieldChanges.at(i).blockHash)
            return listofXfieldChanges.at(i);
    }
    return listofXfieldChanges.back();
}

void CXFieldHistory::InitializeFromBlockDB(TAPYRUS_XFIELDTYPES type, CBlockTreeDB* pblocktree) {
    const char key(std::to_string(static_cast<int8_t>(type))[0]);
    XFieldChangeListWrapper xFieldListDB(key);
    pblocktree->ReadXField(key, xFieldListDB);
    for(auto &XFieldDB:xFieldListDB)
        this->Add(type, XFieldDB);
}

void CXFieldHistory::ToUniValue(TAPYRUS_XFIELDTYPES type, UniValue* xFieldChangeUnival) {
    *xFieldChangeUnival = UniValue(UniValue::VARR);
    XFieldChangeListWrapper& xFieldChangeList = this->operator[](type);
    for (const auto& xFieldChange : xFieldChangeList)
    {
        UniValue xFieldChangeObj(UniValue::VOBJ);
        std::string value = XFieldDataToString(xFieldChange.xfieldValue);
        xFieldChangeObj.pushKV(value, (uint64_t)xFieldChange.height);
        //xFieldChangeObj.push_back(HexStr(xFieldChange.blockHash));
        xFieldChangeUnival->push_back(xFieldChangeObj);
    }
}

bool IsXFieldNew(const CXField& xfield, CXFieldHistoryMap* pxfieldHistory)
{
    IsXFieldLastInHistoryVisitor checkVisitor(pxfieldHistory);
    return xfield.IsValid()
          && std::find(XFIELDTYPES_INIT_LIST.begin(), XFIELDTYPES_INIT_LIST.end(), xfield.xfieldType) != XFIELDTYPES_INIT_LIST.end()
          && !boost::apply_visitor(checkVisitor, xfield.xfieldValue);
}