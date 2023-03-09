// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_XFIELD_H
#define BITCOIN_PRIMITIVES_XFIELD_H

#include <streams.h>
#include <serialize.h>
#include <uint256.h>
#include <key.h>

#include <boost/variant.hpp>
#include <type_traits>
#include <typeinfo>

/*
 * TO add a new xField :
 * 1. Add new type in TAPYRUS_XFIELDTYPES, XFIELDTYPES_INIT_LIST and IsValid
 * 2. Add new class to represent the xfieldValue with all methods defined in XFieldEmpty
 * 3. Update the serialization in XFieldChange, XFieldChangeListWrapper and CXField
 * 4. Update string conversion in XFieldDataToString, GetXFieldNameForRpc and GetXFieldDBKey
 * 5. Add its initialization to CXFieldHistory constructors
 * 6. Add code to verify the property represented by the new xfield during block validation
 */

/*
 * These are the XFIELD types supported in Tapyrus
 */
enum class TAPYRUS_XFIELDTYPES : uint8_t
{
    NONE = 0, //no xfield
    AGGPUBKEY = 1, //xfield is 33 byte aggpubkey
    MAXBLOCKSIZE = 2, //xfield is 4 byte max block size

};

// To check if an xfield in a block is valid. This is used to verify xfield in a block and hence includes NONE
inline bool IsValid(TAPYRUS_XFIELDTYPES type) {
    return type == TAPYRUS_XFIELDTYPES::NONE
        || type == TAPYRUS_XFIELDTYPES::AGGPUBKEY
        || type == TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE;
}

// This list does not include NONE. IT is used to initialize and loop throught xfieldHistory
static const std::initializer_list<TAPYRUS_XFIELDTYPES> XFIELDTYPES_INIT_LIST
{
    TAPYRUS_XFIELDTYPES::AGGPUBKEY,
    TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE
};

/*
 * Following classes represent the XFIELD Values
 * One Value type is associated with one TAPYRUS_XFIELDTYPES
 * The association is enforced using
 * GetXFieldTypeFrom
 * GetXFieldValueFrom
 * 
 */

// TAPYRUS_XFIELDTYPES::NONE
class XFieldEmpty {
public:
    static const char BLOCKTREE_DB_KEY = '0';//unnused key to pass compilation
    XFieldEmpty(){};
    inline bool IsValid() const { return true; }
    inline std::string ToString() const { return ""; }
    inline bool operator==(const XFieldEmpty &x) const { return true; }
};

// TAPYRUS_XFIELDTYPES::AGGPUBKEY
class XFieldAggPubKey {
public:
    std::vector<unsigned char> data;
    static const char BLOCKTREE_DB_KEY = '1';
    explicit XFieldAggPubKey():data() { }
    XFieldAggPubKey(const std::vector<unsigned char>& dataIn):data(dataIn.begin(), dataIn.end()){};
    XFieldAggPubKey(const CPubKey& dataIn):data(dataIn.begin(), dataIn.end()){};

    bool operator==(const std::vector<unsigned char>& dataIn) const { return data == dataIn; }
    bool operator==(const XFieldAggPubKey& xfield) const { return xfield.operator==(this->data); }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(data);
    }

    inline bool IsValid() const {
        return CPubKey(data.begin(), data.end()).IsFullyValid();
    }

    inline std::string ToString() const;
    CPubKey inline getPubKey() const { return CPubKey(data.begin(), data.end()); }
};

// TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE
class XFieldMaxBlockSize {
public:
    uint32_t data;
    static const char BLOCKTREE_DB_KEY = '2';
    explicit XFieldMaxBlockSize():data() { }
    XFieldMaxBlockSize(uint32_t dataIn):data(dataIn) { }

    bool operator==(uint32_t dataIn) const { return data == dataIn; }
    bool operator==(const XFieldMaxBlockSize& xfield) const { return xfield.operator==(this->data); }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(data);
    }

    inline bool IsValid() const {
        //min based on BlockAssembler(1K and MAX_BLOCK_SIZE-1K)
        return data > 1000;
    }

    inline std::string ToString() const;
};

/*
 * union of above classes to represent xfieldValue.
 */
typedef boost::variant<
    XFieldEmpty,
    XFieldAggPubKey,
    XFieldMaxBlockSize> XFieldData;

/*
 * This method defines the association between XFieldData and TAPYRUS_XFIELDTYPES
 */
inline TAPYRUS_XFIELDTYPES GetXFieldTypeFrom(XFieldData xfieldDataIn) {
    return TAPYRUS_XFIELDTYPES(xfieldDataIn.which());
}
/*
 * This method defines the association between TAPYRUS_XFIELDTYPES and XFieldData classes
 */

template <typename T>
bool GetXFieldValueFrom(XFieldData& xfieldValue, T& value);

/*
 * struct to manipulate Xfield(type and value) together as one entity.
 * This is serialized in the block header.
 * xfieldType and xfieldValue are convertible using GetXFieldTypeFrom and GetXFieldValueFrom
 *
 */
struct CXField {

    TAPYRUS_XFIELDTYPES xfieldType;
    XFieldData xfieldValue;

    //default constructor
    CXField():xfieldType(TAPYRUS_XFIELDTYPES::NONE){}

    //constructor
    CXField(XFieldData xfieldValueIn) {
        xfieldValue = xfieldValueIn;
        xfieldType = GetXFieldTypeFrom(xfieldValueIn);
    }

    //copy constructor
    CXField(const CXField& copy){
        xfieldValue = copy.xfieldValue;
        xfieldType = GetXFieldTypeFrom(copy.xfieldValue);
    }
    explicit CXField(CXField& copy){
        xfieldValue = copy.xfieldValue;
        xfieldType = GetXFieldTypeFrom(copy.xfieldValue);
    }

    //move constructor
    CXField(const CXField&& copy){
        xfieldValue = copy.xfieldValue;
        xfieldType = GetXFieldTypeFrom(copy.xfieldValue);
    }
    explicit CXField(CXField&& copy){
        xfieldValue = copy.xfieldValue;
        xfieldType = GetXFieldTypeFrom(copy.xfieldValue);
    }

    //copy assignment
    CXField& operator=(const CXField& copy){
        xfieldValue = copy.xfieldValue;
        xfieldType = GetXFieldTypeFrom(copy.xfieldValue);
        return *this;
    }
    CXField& operator=(CXField& copy){
        xfieldValue = copy.xfieldValue;
        xfieldType = GetXFieldTypeFrom(copy.xfieldValue);
        return *this;
    }

    //operator ==
    bool operator==(const CXField& copy){
        return xfieldType == copy.xfieldType
            && xfieldValue == copy.xfieldValue;
    }
    bool operator==(CXField& copy){
        return xfieldType == copy.xfieldType
            && xfieldValue == copy.xfieldValue;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ::Serialize(s, (int8_t)xfieldType);
        switch(xfieldType)
        {
            case TAPYRUS_XFIELDTYPES::AGGPUBKEY:
                ::Serialize(s, boost::get<XFieldAggPubKey>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
                ::Serialize(s, boost::get<XFieldMaxBlockSize>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::NONE:
                break;
        }
        assert(GetXFieldTypeFrom(xfieldValue) == xfieldType);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        uint8_t temp;
        ::Unserialize(s, temp);
        xfieldType = (TAPYRUS_XFIELDTYPES)temp;
        switch(xfieldType)
        {
            case TAPYRUS_XFIELDTYPES::AGGPUBKEY: {
                XFieldAggPubKey value;
                ::Unserialize(s, value);
                xfieldValue = value; break;
            }
            case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE: {
                XFieldMaxBlockSize value;
                ::Unserialize(s, value);
                xfieldValue = value; break;
            }
            case TAPYRUS_XFIELDTYPES::NONE:
                break;
        }
    }

    inline void clear() {
        xfieldType = TAPYRUS_XFIELDTYPES::NONE;
        xfieldValue = XFieldEmpty();
    }

    std::string ToString() const;

    bool IsValid() const;
};


class XFieldValidityVisitor : public boost::static_visitor< bool >
{
public:

    template <typename T>
    inline bool operator()(const T &xField) const {
        return xField.IsValid();
    }
};

inline bool IsXFieldValid(const CXField& xField) { return xField.IsValid(); }

std::string XFieldDataToString(const XFieldData &xfieldValue);

char GetXFieldDBKey(const XFieldData& xFieldValue);

#endif // BITCOIN_PRIMITIVES_XFIELD_H