// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/xfield.h>
#include <tinyformat.h>
#include <utilstrencodings.h>
#include <variant>

inline std::string XFieldAggPubKey::ToString() const {
    return HexStr(data);
}

inline std::string XFieldMaxBlockSize::ToString() const {
    return std::to_string(data);
}

template <typename T>
bool GetXFieldValueFrom(XFieldData& xfieldValue, T& value) {
    value = std::get<T>(xfieldValue);
    // Convert character digit to integer without locale dependency
    return (value.BLOCKTREE_DB_KEY - '0') == static_cast<int>(GetXFieldTypeFrom(xfieldValue));
}

std::string CXField::ToString() const {
    std::stringstream s;
    s << strprintf("CXField(xfieldType=%d, xfieldValue={%s})\n",
        (uint8_t)xfieldType, XFieldDataToString(xfieldValue).c_str());
    return s.str();
}

bool CXField::IsValid() const {
    return ::IsValid(this->xfieldType)
        && std::visit(XFieldValidityVisitor(), this->xfieldValue)
        && GetXFieldTypeFrom(this->xfieldValue) == this->xfieldType;
}

std::string XFieldDataToString(const XFieldData &xfieldValue) {
    switch(GetXFieldTypeFrom(xfieldValue))
    {
        case TAPYRUS_XFIELDTYPES::AGGPUBKEY:
            return std::get<XFieldAggPubKey>(xfieldValue).ToString();
        case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
            return std::get<XFieldMaxBlockSize>(xfieldValue).ToString();
        case TAPYRUS_XFIELDTYPES::NONE:
        default:
            return "";
    }
}


char GetXFieldDBKey(const XFieldData& xfieldValue) {
    switch(GetXFieldTypeFrom(xfieldValue))
    {
        case TAPYRUS_XFIELDTYPES::AGGPUBKEY:
            return std::get<XFieldAggPubKey>(xfieldValue).BLOCKTREE_DB_KEY;
        case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
            return std::get<XFieldMaxBlockSize>(xfieldValue).BLOCKTREE_DB_KEY;
        case TAPYRUS_XFIELDTYPES::NONE:
        default:
            return '\0';
    }
}

void BadXFieldException::constructMessage(TAPYRUS_XFIELDTYPES type, XFieldData xfieldValue) {
    std::ostringstream oss;
    if (unknown) {
        oss << "Upgrade node. Unknown xfield found in block. Node cannot sync to the blockchain with xfieldType=" << (uint8_t)type << std::endl;
    } else {
        oss << "Type and data mismatch in CXField. xfieldType=" << (uint8_t)type <<"  expected =" << (uint8_t)GetXFieldTypeFrom(xfieldValue) << std::endl;
    }
    message = oss.str();
}