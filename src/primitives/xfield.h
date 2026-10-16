// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_XFIELD_H
#define BITCOIN_PRIMITIVES_XFIELD_H

#include <primitives/xfieldvalues.h>
#include <serialize.h>
#include <streams.h>

#include <string>
#include <type_traits>
#include <typeinfo>

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
                ::Serialize(s, std::get<XFieldAggPubKey>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
                ::Serialize(s, std::get<XFieldMaxBlockSize>(xfieldValue)); break;
            case TAPYRUS_XFIELDTYPES::NONE:
            default:
                break;
        }
        if(GetXFieldTypeFrom(xfieldValue) != xfieldType)
            throw BadXFieldException(xfieldType, xfieldValue);
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
                // No payload bytes for NONE. Any bytes that a peer appended
                // after the type byte are NOT consumed here — they will be
                // read by the next field (proof). CheckBlockHeader catches
                // this by requiring proof to be exactly SCHNORR_SIGNATURE_SIZE
                // bytes before attempting verification.
                break;
            default:
                // Unknown xfield type: the consistency check below will throw
                // BadXFieldException, but an explicit default makes the intent
                // clear and prevents silent fallthrough for future enum values.
                throw BadXFieldException(xfieldType, xfieldValue);
        }
        if(GetXFieldTypeFrom(xfieldValue) != xfieldType) {
            throw BadXFieldException(xfieldType, xfieldValue);
        }
    }

    inline void clear() {
        xfieldType = TAPYRUS_XFIELDTYPES::NONE;
        xfieldValue = XFieldEmpty();
    }

    std::string ToString() const;

    bool IsValid() const;
};


class XFieldValidityVisitor
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
