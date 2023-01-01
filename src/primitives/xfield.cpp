// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/xfield.h>
#include <tinyformat.h>
#include <utilstrencodings.h>

inline std::string XFieldAggPubKey::ToString() const {
    return HexStr(data);
}

XFieldAggPubKey::XFieldAggPubKey(const std::vector<unsigned char>& pubkey):data(pubkey) {
    if(!pubkey.size())
        throw std::runtime_error("Aggregate Public Key for Signed Block is empty");
    
    if (pubkey[0] == 0x02 || pubkey[0] == 0x03) {
        CPubKey aggPubKey(pubkey.begin(), pubkey.end());
        if(!aggPubKey.IsFullyValid()) {
            throw std::runtime_error(strprintf("Aggregate Public Key for Signed Block is invalid: %s", HexStr(pubkey)));
        }

        if (aggPubKey.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) {
            throw std::runtime_error(strprintf("Aggregate Public Key for Signed Block is invalid: %s size was: %d", HexStr(pubkey), aggPubKey.size()));
        }
        return;

    } else if(pubkey[0] == 0x04 || pubkey[0] == 0x06 || pubkey[0] == 0x07) {
        throw std::runtime_error(strprintf("Uncompressed public key format are not acceptable: %s", HexStr(pubkey)));
    }
    throw std::runtime_error(strprintf("Aggregate Public Key for Signed Block is invalid: %s", HexStr(pubkey)));
}

inline std::string XFieldMaxBlockSize::ToString() const {
    return HexStr(&data, &data + sizeof(data));
}

template <typename T>
bool GetXFieldValueFrom(XFieldData& xfieldValue, T& value) {
    value = boost::get<T>(xfieldValue);
    return std::atoi(value.BLOCKTREE_DB_KEY) == GetXFieldTypeFrom(xfieldValue);
}

std::string CXField::ToString() const {
    std::stringstream s;
    s << strprintf("CXField(xfieldType=%d, xfieldValue={%s})\n",
        (uint8_t)xfieldType, XFieldDataToString(xfieldValue).c_str());
    return s.str();
}

bool CXField::IsValid() const {
    return ::IsValid(this->xfieldType)
        && boost::apply_visitor(XFieldValidityVisitor(), this->xfieldValue)
        && GetXFieldTypeFrom(this->xfieldValue) == this->xfieldType;
}

std::string XFieldDataToString(const XFieldData &xfieldValue) {
    switch(GetXFieldTypeFrom(xfieldValue))
    {
        case TAPYRUS_XFIELDTYPES::AGGPUBKEY:
            return boost::get<XFieldAggPubKey>(xfieldValue).ToString();
        case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
            return boost::get<XFieldMaxBlockSize>(xfieldValue).ToString();
        case TAPYRUS_XFIELDTYPES::NONE:
        default:
            return "";
    }
}


char GetXFieldDBKey(const XFieldData& xfieldValue) {
    switch(GetXFieldTypeFrom(xfieldValue))
    {
        case TAPYRUS_XFIELDTYPES::AGGPUBKEY:
            return boost::get<XFieldAggPubKey>(xfieldValue).BLOCKTREE_DB_KEY;
        case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
            return boost::get<XFieldMaxBlockSize>(xfieldValue).BLOCKTREE_DB_KEY;
        case TAPYRUS_XFIELDTYPES::NONE:
        default:
            return '\0';
    }
}

const char* BadXFieldException::what() const noexcept
{
    if(unknown)
        return strprintf("Upgrade node. Unknown xfield found in block. Node cannot sync to the blockchain with xfieldType=%s", std::to_string((uint8_t)type).c_str()).c_str();
    else
        return strprintf("Type and data mismatch in CXField. xfieldType=%s  xfieldValue=%s", std::to_string((uint8_t)type).c_str(), XFieldDataToString(xfieldValue).c_str()).c_str();
}