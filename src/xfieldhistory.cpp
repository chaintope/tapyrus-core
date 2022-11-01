// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/xfield.h>
#include <xfieldhistory.h>
#include <txdb.h>
#include <univalue.h>

std::map< const TAPYRUS_XFIELDTYPES, std::vector<XFieldChange> >  XFieldHistory::XFieldHistoryMap;

void XFieldHistory::Add(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange) {
    XFieldHistoryMap[type].push_back(xFieldChange);
}

void XFieldHistory::Remove(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange) {
    //XFieldHistoryMap[type].erase(xFieldChange);
}

const XFieldChange& XFieldHistory::Get(TAPYRUS_XFIELDTYPES type, uint32_t height) {

    const std::vector<XFieldChange>& listofXfieldChanges = XFieldHistoryMap[type];

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

const XFieldChange& XFieldHistory::Get(TAPYRUS_XFIELDTYPES type, uint256 blockHash) {

    const std::vector<XFieldChange>& listofXfieldChanges = XFieldHistoryMap[type];
    //TODO: return the corrext xfield for any block.
    //do not use == for this search
    for(unsigned int i = 0; i < listofXfieldChanges.size(); i++) {
        if(blockHash == listofXfieldChanges.at(i).blockHash)
            return listofXfieldChanges.at(i);
    }
    return listofXfieldChanges.back();
}

/*
CPubKey XFieldHistory::GetAggPubkeyFromHeight(uint32_t height) const
{
    auto& aggPubKey = boost::get<XFieldAggPubKey>(Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, height).xfieldValue).aggPubKey;
    return CPubKey(aggPubKey.begin(), aggPubKey.end());
}

uint32_t XFieldHistory::GetMaxBlockSizeFromHeight(uint32_t height) const
{
    return boost::get<XFieldMaxBlockSize>(Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, height).xfieldValue).maxBlockSize;
}

std::vector<XFieldChange>* XFieldHistory::GetAggregatePubkeyHeightList() const
{
    if(XFieldHistoryMap.find(TAPYRUS_XFIELDTYPES::AGGPUBKEY) == XFieldHistoryMap.end())
        return nullptr;
    return &XFieldHistoryMap[TAPYRUS_XFIELDTYPES::AGGPUBKEY];
}

std::vector<XFieldChange>* XFieldHistory::GetMaxBlockSizeHeightList() const
{
    if(XFieldHistoryMap.find(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE) == XFieldHistoryMap.end())
        return nullptr;
    return &XFieldHistoryMap[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE];
}

uint32_t XFieldHistory::GetHeightFromAggregatePubkey(const CPubKey &aggpubkey) const
{
    for (auto& c : XFieldHistoryMap[TAPYRUS_XFIELDTYPES::AGGPUBKEY]) {
        if (c.xfieldValue == aggpubkey)
            return c.height;
    }
    return -1;
}

uint32_t XFieldHistory::GetHeightFromMaxBlockSize(const uint32_t maxBlockSize) const
{
    for (auto& c : XFieldHistoryMap[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE]) {
        if (c.xfieldValue == maxBlockSize)
            return c.height;
    }
    return -1;
}

CPubKey XFieldHistory::XFieldAggPubKey aggpubkeyChange;
    xFieldHistory.GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);
    CPubKey aggpubkey(aggpubkeyChange.getPubKey());() const {
    XFieldAggPubKey& aggPubKey = boost::get<XFieldAggPubKey>(Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, height).xfieldValue).aggPubKey;
    return CPubKey(aggPubKey.begin(), aggPubKey.end());
}
uint32_t XFieldHistory::GetLatestMaxBlockSiz() const {
    return boost::get<XFieldMaxBlockSize>(XFieldHistoryMap[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].rbegin().xfieldValue).maxBlockSize;
}

bool RemoveAggregatePubkey(const CPubKey &aggpubkey ) {
    auto & AggPubKeyChangeList = XFieldHistoryMap[TAPYRUS_XFIELDTYPES::AGGPUBKEY];

    AggPubKeyChangeList.iterator findIter =  std::find_if(AggPubKeyChangeList.begin(), AggPubKeyChangeList.end(), [aggpubkey](XFieldChange* iter){ boost::get<XFieldAggPubKey*>(iter)->aggPubKey == aggpubkey; });

    if(findIter == AggPubKeyChangeList.end())
        return false;
    AggPubKeyChangeList.erase(findIter);
    return true;
}
bool RemoveMaxBlockSize(const uint32_t maxBlockSize) {
    auto & MaxBlockSizeChangeList = XFieldHistoryMap[TAPYRUS_XFIELDTYPES::AGGPUBKEY];

    MaxBlockSizeChangeList.iterator findIter =  std::find_if(MaxBlockSizeChangeList.begin(), MaxBlockSizeChangeList.end(), [aggpubkey](XFieldChange* iter){ boost::get<XFieldMaxBlockSize*>(iter)->maxBlockSize == maxBlockSize; });

    if(findIter == MaxBlockSizeChangeList.end())
        return false;
    MaxBlockSizeChangeList.erase(findIter);
    return true;
}*/

void XFieldHistory::InitializeFromBlockDB(TAPYRUS_XFIELDTYPES type, CBlockTreeDB* pblocktree) {
    std::vector<XFieldChange> xFieldListDB;
    pblocktree->Read(static_cast<char>(type), xFieldListDB);
    for(auto &XFieldDB:xFieldListDB)
        this->Add(type, XFieldDB);
}

void XFieldHistory::ToUniValue(TAPYRUS_XFIELDTYPES type, UniValue* xFieldChangeUnival) {
    *xFieldChangeUnival = UniValue(UniValue::VARR);
    const std::vector<XFieldChange>* xFieldChangeList = this->operator[](type);
    for (auto& xFieldChange : * xFieldChangeList)
    {
        UniValue xFieldChangeObj(UniValue::VOBJ);
        std::string value = XFieldDataToString(xFieldChange.xfieldValue);
        xFieldChangeObj.push_back(value);
        xFieldChangeObj.push_back((uint64_t)xFieldChange.height);
        xFieldChangeObj.push_back(HexStr(xFieldChange.blockHash));
        xFieldChangeUnival->push_back(xFieldChangeObj);
    }
}

bool IsXFieldNewToHistory(const CXField& xfield)
{
    return xfield.IsValid()
          && std::find(XFIELDTYPES_INIT_LIST.begin(), XFIELDTYPES_INIT_LIST.end(), xfield.xfieldType) != XFIELDTYPES_INIT_LIST.end()
          && !boost::apply_visitor(XFieldIsLastInHistoryVisitor(), xfield.xfieldValue);
}