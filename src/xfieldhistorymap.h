// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_XFIELDHISTORYMAP_H
#define TAPYRUS_XFIELDHISTORYMAP_H

#include <primitives/xfield.h>
#include <serialize.h>
#include <uint256.h>

#include <cassert>
#include <map>
#include <shared_mutex>
#include <stdexcept>
#include <vector>

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
    ~XFieldChange() = default;

    bool operator==(const XFieldChange& copy) const {
        return xfieldValue == copy.xfieldValue
            && height == copy.height
            && blockHash == copy.blockHash;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        switch(GetXFieldTypeFrom(xfieldValue))
        {
            case TAPYRUS_XFIELDTYPES::AGGPUBKEY:
                ::Serialize(s, std::get<XFieldAggPubKey>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
                ::Serialize(s, std::get<XFieldMaxBlockSize>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::NONE:
            default:
                break;
        }
        ::Serialize(s, height);
        ::Serialize(s, blockHash);
    }
    //no unserialize method in this class.
    //it is implemented in XFieldChangeListWrapper
};

typedef std::vector<XFieldChange> XFieldChangeList;

/*
 * helper class to unserialize vector of XFieldChange from blocktree db
 * entry containing only XFieldData, height and block hash (no XFieldType).
 * It is not possible to know the type and therefore the size of data to read from the stream.
 * We store the key in this class to help in unserialization of XFieldData.
 */
struct XFieldChangeListWrapper
{
    char key;
    XFieldChangeList xfieldChanges;

    explicit XFieldChangeListWrapper(char keyIn):key(keyIn),xfieldChanges(){}

    //methods to simulate vector
    inline size_t size() const { return xfieldChanges.size();}
    inline XFieldChangeList::iterator begin() {return xfieldChanges.begin();}
    inline XFieldChangeList::iterator end() {return xfieldChanges.end();}
    inline XFieldChangeList::reverse_iterator rbegin() {return xfieldChanges.rbegin();}
    inline XFieldChangeList::reverse_iterator rend() {return xfieldChanges.rend();}
    inline const XFieldChange& back() const {return xfieldChanges.back();}
    inline const XFieldChange& at(size_t i) const {return xfieldChanges.at(i);}
    inline const XFieldChange& operator[](size_t i) const { return xfieldChanges.operator[](i);}

    inline void push_back(const XFieldChange& item) { return xfieldChanges.push_back(item); }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ::Serialize(s, xfieldChanges);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        uint32_t len = 0;
        s >> VARINT(len);
        while(s.size())
        {
            XFieldChange xfieldChange;
            switch(key)
            {
                case XFieldAggPubKey::BLOCKTREE_DB_KEY: {
                    XFieldAggPubKey value;
                    ::Unserialize(s, value);
                    xfieldChange.xfieldValue = value; break;
                }
                case XFieldMaxBlockSize::BLOCKTREE_DB_KEY: {
                    XFieldMaxBlockSize value;
                    ::Unserialize(s, value);
                    xfieldChange.xfieldValue = value; break;
                }
                default:
                    throw std::runtime_error("unknown xfield found. restart node with -reloadxfield option to correct the db.");
            }
            ::Unserialize(s, xfieldChange.height);
            ::Unserialize(s, xfieldChange.blockHash);
            xfieldChanges.push_back(xfieldChange);
        }
        assert(len == xfieldChanges.size());
    }
};

typedef std::map< TAPYRUS_XFIELDTYPES, XFieldChangeListWrapper > XFieldHistoryMapType;
/*  This map is used instead of the list in federation params after v0.5.2
 *
 * XFieldHistoryMapType is a Global map to store all xfield changes.
 *
 * class CXFieldHistoryMap contains a static map of XFieldHistoryMapType.
 * (All objects of this class read and write to the same map.) 
 * This is the full list of xfield change in the active chain.
 * When the isTemp flag is set in the constructor, all methods access the temp map.
 *
 * Note that this class is pure virtual. It is always accessed via CXFieldHistory or CTempXFieldHistory.
 */
class CXFieldHistoryMap{

    const bool isTemp;

protected:
    static XFieldHistoryMapType xfieldHistory;
    static std::shared_mutex xfieldHistoryMutex; 
    inline CXFieldHistoryMap(bool temp):isTemp(temp) { }

public:
    virtual ~CXFieldHistoryMap(){}
    virtual XFieldHistoryMapType& getXFieldHistoryMap() const = 0;

    template <typename T>
    void GetLatest(TAPYRUS_XFIELDTYPES type, T & xfieldval) const {
        std::shared_lock<std::shared_mutex> lock(xfieldHistoryMutex);
        auto& listofXfieldChanges = (isTemp ? this->getXFieldHistoryMap() : xfieldHistory).find(type)->second;
        xfieldval = std::get<T>(listofXfieldChanges.rbegin()->xfieldValue);
    }

    // Thread-safe methods to access xfield history data
    virtual size_t GetListSize(TAPYRUS_XFIELDTYPES type) const;
    virtual XFieldChangeList GetListCopy(TAPYRUS_XFIELDTYPES type) const;

    virtual void Add(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange);
    // Remove the last entry for `type` if and only if its blockHash matches `xFieldChange`.
    // Returns true when an entry was removed; false when the list has only the genesis entry
    // or the last entry does not match (safe no-op in that case).
    bool Remove(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange);

    virtual XFieldChange Get(TAPYRUS_XFIELDTYPES type, uint32_t height);
    int32_t GetReorgHeight();
};

#endif // TAPYRUS_XFIELDHISTORYMAP_H
