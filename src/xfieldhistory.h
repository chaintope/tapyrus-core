// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_XFIELD_HISTORY_H
#define BITCOIN_XFIELD_HISTORY_H

#include <policy/policy.h>
/* 
 * struct to store xfieldValue, block hash and height for every xfield update in the blockchain.
 */

struct XFieldChange {
    XFieldData xfieldValue;
    uint32_t height;
    uint256 blockHash;

    XFieldChange():xfieldValue(), height(0), blockHash(){}
    explicit XFieldChange(const XFieldData& in_data, uint32_t in_height, uint256 in_blockHash):xfieldValue(in_data), height(in_height), blockHash(in_blockHash){}
    explicit XFieldChange(const XFieldData&& in_data, uint32_t in_height, uint256 in_blockHash):xfieldValue(in_data), height(in_height), blockHash(in_blockHash){}
    XFieldChange(const XFieldChange& copy):xfieldValue(copy.xfieldValue),height(copy.height),blockHash(copy.blockHash){}
    XFieldChange(const XFieldChange&& copy):xfieldValue(copy.xfieldValue),height(copy.height),blockHash(copy.blockHash){}
    ~XFieldChange() = default;


    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        switch(GetXFieldTypeFrom(xfieldValue))
        {
            case TAPYRUS_XFIELDTYPES::AGGPUBKEY:
                READWRITE(boost::get<XFieldAggPubKey>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
                READWRITE(boost::get<XFieldMaxBlockSize>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::NONE:
            default:
                break;
        }
        READWRITE(height);
        READWRITE(blockHash);
    }
};

class CBlockTreeDB;
class UniValue;
/* 
Global map to store all xfield changes.
This is moved out of federation params.
*/

class XFieldHistory{

    static std::map< const TAPYRUS_XFIELDTYPES, std::vector<XFieldChange> > XFieldHistoryMap;

public:
    XFieldHistory() {}
    XFieldHistory(const CBlock& genesis) {
        if(XFieldHistoryMap.size() != 0)
            return;
        this->Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(genesis.xfield.xfieldValue, 0, genesis.GetHash()));
        this->Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(DEFAULT_BLOCK_MAX_SIZE, 0, genesis.GetHash()));
    }

    const std::vector<XFieldChange>* operator[](TAPYRUS_XFIELDTYPES type) const { return &XFieldHistoryMap[type];}

    void InitializeFromBlockDB(TAPYRUS_XFIELDTYPES type, CBlockTreeDB* pblocktree);
    void Add(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange);
    void Remove(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange);
    void ToUniValue(TAPYRUS_XFIELDTYPES type, UniValue* xFieldChangeList);

    const XFieldChange& Get(TAPYRUS_XFIELDTYPES type, uint32_t height);
    const XFieldChange& Get(TAPYRUS_XFIELDTYPES type, uint256 blockHash);

    template <typename T>
    void GetLatest(TAPYRUS_XFIELDTYPES type, T & xfieldval) const {
        const std::vector<XFieldChange>& listofXfieldChanges = XFieldHistoryMap[type];
        xfieldval = boost::get<T>(listofXfieldChanges.rbegin()->xfieldValue);
    }
    /*uint32_t GetHeightFromAggregatePubkey(const CPubKey &aggpubkey) const;
    CPubKey GetAggPubkeyFromHeight(uint32_t height) const;
    CPubKey AddAggregatePubkey(const std::vector<unsigned char>& pubkey, uint32_t height);
    std::vector<XFieldChange>* GetAggregatePubkeyHeightList() const;
    CPubKey XFieldAggPubKey aggpubkeyChange;
    xFieldHistory.GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);
    CPubKey aggpubkey(aggpubkeyChange.getPubKey());() const;
    bool RemoveAggregatePubkey(const CPubKey &aggpubkey );

    uint32_t GetHeightFromMaxBlockSize(const uint32_t maxBlockSize) const;
    uint32_t GetMaxBlockSizeFromHeight(uint32_t height) const;
    uint32_t AddMaxBlockSize(const uint32_t maxBlockSize, uint32_t height);
    const std::vector<XFieldChange>* GetMaxBlockSizeHeightList() const;
    uint32_t GetLatestMaxBlockSiz() const;
    bool RemoveMaxBlockSize(const uint32_t maxBlockSize);*/
};


class XFieldIsLastInHistoryVisitor : public boost::static_visitor< bool >
{
public:

    template <typename T>
    bool operator()(const T &xField) const {
        TAPYRUS_XFIELDTYPES X = GetXFieldTypeFrom(xField);
        XFieldHistory history;
        return boost::get<T>(history[X]->rbegin()->xfieldValue).operator==(T(xField));
    }

};

bool IsXFieldNewToHistory(const CXField& xfield);


#endif // BITCOIN_XFIELD_HISTORY_H