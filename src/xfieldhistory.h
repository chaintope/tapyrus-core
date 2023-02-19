// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_XFIELDHISTORY_H
#define TAPYRUS_XFIELDHISTORY_H

#include <policy/policy.h>
#include <federationparams.h>
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
                ::Serialize(s, boost::get<XFieldAggPubKey>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
                ::Serialize(s, boost::get<XFieldMaxBlockSize>(xfieldValue)); break;
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
 * db entry contains only XFieldData, height and block hash (no XFieldType).
 * It is not possible to know the type and therefore the size of data to read from the stream.
 * We store a the key in this class to help in unserialization of XFieldData.
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

    TAPYRUS_XFIELDTYPES getXFieldType(const char key) {
        return static_cast<TAPYRUS_XFIELDTYPES>( static_cast<uint8_t>(key));
    }

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
                    break;
            }
            ::Unserialize(s, xfieldChange.height);
            ::Unserialize(s, xfieldChange.blockHash);
            xfieldChanges.push_back(xfieldChange);
        }
        assert(len == xfieldChanges.size());
    }
};

class CBlockTreeDB;
class UniValue;

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
    //critical section to wait for xfield history to be initialized
    static CWaitableCriticalSection cs_XFieldHistoryWait;
    //conditional variable to notify other waiting threads
    static CConditionVariable condvar_XFieldHistoryWait;

    static XFieldHistoryMapType xfieldHistory;
    inline CXFieldHistoryMap(bool temp):isTemp(temp) { }

public:
    virtual ~CXFieldHistoryMap(){}
    virtual XFieldHistoryMapType& getXFieldHistoryMap() const = 0;

    template <typename T>
    void GetLatest(TAPYRUS_XFIELDTYPES type, T & xfieldval) const {
        auto& listofXfieldChanges = (isTemp ? this->getXFieldHistoryMap() : xfieldHistory).find(type)->second;
        while(true){
            if(listofXfieldChanges.size())
                break;
            WaitableLock lock(cs_XFieldHistoryWait);
            condvar_XFieldHistoryWait.wait_for(lock, std::chrono::milliseconds(500));
            listofXfieldChanges = (isTemp ? this->getXFieldHistoryMap() : xfieldHistory).find(type)->second;
        }
        
        xfieldval = boost::get<T>(listofXfieldChanges.rbegin()->xfieldValue);
    }

    virtual XFieldChangeListWrapper& operator[](TAPYRUS_XFIELDTYPES type) const {
        return xfieldHistory.find(type)->second;
    }

    virtual bool IsNew(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange) const;
    virtual void Add(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange);
    //void Remove(TAPYRUS_XFIELDTYPES type, const XFieldChange& xFieldChange);

    virtual const XFieldChange& Get(TAPYRUS_XFIELDTYPES type, uint32_t height);
    virtual const XFieldChange& Get(TAPYRUS_XFIELDTYPES type, uint256 blockHash);
    int32_t GetReorgHeight();
};

/*
 * class CXFieldHistory is an interface to access the global map of xfiled changes
 * in the active chain which is guaranteed to be initialized.
 * this defines ots own constructor and other utility methods that act on the data in CXFieldHistoryMap
 */
class CXFieldHistory : public CXFieldHistoryMap{

public:
    CXFieldHistory():CXFieldHistoryMap(false) {}
    virtual ~CXFieldHistory(){}

    //constructor to initialize the confirmed global map
    inline explicit CXFieldHistory(const CBlock& genesis):CXFieldHistoryMap(false) {
        WaitableLock lock_GenesisWait(cs_XFieldHistoryWait);

        xfieldHistory.emplace(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChangeListWrapper(XFieldAggPubKey::BLOCKTREE_DB_KEY));
        xfieldHistory.emplace(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChangeListWrapper(XFieldMaxBlockSize::BLOCKTREE_DB_KEY));

        this->Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(genesis.xfield.xfieldValue, 0, genesis.GetHash()));
        this->Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(MAX_BLOCK_SIZE, 0, genesis.GetHash()));

        condvar_XFieldHistoryWait.notify_all();
    }

    XFieldHistoryMapType& getXFieldHistoryMap() const override {
        return xfieldHistory;
    }

    void ToUniValue(TAPYRUS_XFIELDTYPES type, UniValue* xfieldChanges);
};

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
        WaitableLock lock(cs_XFieldHistoryWait);
        condvar_XFieldHistoryWait.wait_for(lock, std::chrono::milliseconds(500));

        xfieldHistoryTemp = new XFieldHistoryMapType;
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

    XFieldChangeListWrapper& operator[](TAPYRUS_XFIELDTYPES type) const override {
        return xfieldHistoryTemp->find(type)->second;
    }

};

class IsXFieldLastInHistoryVisitor : public boost::static_visitor< bool >
{
    CXFieldHistoryMap* history;
public:
    IsXFieldLastInHistoryVisitor(CXFieldHistoryMap* historyIn):history(historyIn) {}

    template <typename T>
    bool operator()(const T &xField) const {
        assert(history);
        TAPYRUS_XFIELDTYPES X = GetXFieldTypeFrom(xField);
        return boost::get<T>((*history)[X].rbegin()->xfieldValue).operator==(T(xField));
    }

};

bool IsXFieldNew(const CXField& xfield, CXFieldHistoryMap* pxfieldHistory);


#endif // TAPYRUS_XFIELDHISTORY_H