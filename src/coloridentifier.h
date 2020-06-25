// Copyright (c) 2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef TAPYRUS_COLORIDENTIFIER_H
#define TAPYRUS_COLORIDENTIFIER_H

#include <crypto/sha256.h>
#include <streams.h>
#include <version.h>
#include <amount.h>

enum class TokenTypes
{
    NONE = 0x00, //TPC
    REISSUABLE = 0xc1,
    NON_REISSUABLE = 0xc2,
    NFT = 0xc3,
    TOKENTYPE_MAX = NFT
};

inline uint8_t TokenToUint(TokenTypes t)
{
    switch(t)
    {
        case TokenTypes::NONE: return 0x00;
        case TokenTypes::REISSUABLE: return 0xc1;
        case TokenTypes::NON_REISSUABLE: return 0xc2;
        case TokenTypes::NFT: return 0xc3;
        default: return 0x00;
    }
}

inline TokenTypes UintToToken(uint8_t t)
{
    switch(t)
    {
        case 0x00: return TokenTypes::NONE;
        case 0xc1: return TokenTypes::REISSUABLE;
        case 0xc2: return TokenTypes::NON_REISSUABLE;
        case 0xc3: return TokenTypes::NFT;
        default: return TokenTypes::NONE;
    }
}

struct ColorIdentifier
{
    TokenTypes type;
    uint8_t payload[CSHA256::OUTPUT_SIZE];

    ColorIdentifier():type(TokenTypes::NONE), payload{} { }

    ColorIdentifier(COutPoint &utxoIn, TokenTypes typeIn):type(typeIn), payload{} {
        CDataStream s(SER_NETWORK, INIT_PROTO_VERSION);
        s << utxoIn;
        CSHA256().Write((unsigned char *)s.data(), s.size()).Finalize(payload);
    }

    ColorIdentifier(const CScript& input):type(TokenTypes::REISSUABLE), payload{} {
        std::vector<unsigned char> scriptVector(input.begin(), input.end());
        CSHA256().Write(scriptVector.data(), scriptVector.size()).Finalize(payload);
    }

    ColorIdentifier(const unsigned char* pbegin, const unsigned char* pend):type(UintToToken(*pbegin)) {
        CSerActionUnserialize ser_action;
        CDataStream s((const char*)pbegin, (const char*)pend, SER_NETWORK, INIT_PROTO_VERSION);
        SerializationOp(s, ser_action);
     }

    ColorIdentifier(const std::vector<unsigned char>& in) {
        CSerActionUnserialize ser_action;
        CDataStream s(in, SER_NETWORK, INIT_PROTO_VERSION);
        SerializationOp(s, ser_action);
     }

    bool operator==(const ColorIdentifier& colorId) const {
        return this->type == colorId.type && (memcmp(&this->payload[0], &colorId.payload[0], 32) == 0);
    }

    bool operator<(const ColorIdentifier& colorId) const {
        return memcmp(this, &colorId, 33) < 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead())
        {
            char xtype;
            s.read(&xtype, 1);
            type = UintToToken((uint8_t)xtype);
        }
        else
        {
            const uint8_t xtype = TokenToUint(type);
            s.write((const char *)&xtype, 1);
        }

        if(type > TokenTypes::NONE && type <= TokenTypes::TOKENTYPE_MAX)
            READWRITE(this->payload);
    }

    inline std::vector<unsigned char> toVector() const {
        CDataStream stream(SER_NETWORK, INIT_PROTO_VERSION);
        this->Serialize(stream);
        return std::vector<unsigned char>(stream.begin(), stream.end());
    }

};

ColorIdentifier GetColorIdFromScript(const CScript& script);

//this is needed to verify token balances as using a custom class as map key 
//needs a comparison operator to order the map

struct ColorIdentifierCompare
{
    bool operator()(const ColorIdentifier& c1, const ColorIdentifier& c2) const
    {
        return memcmp(&c1, &c2, 33) < 0;
    }
};

typedef std::map<ColorIdentifier, CAmount, ColorIdentifierCompare> TxColoredCoinBalancesMap;


#endif //TAPYRUS_COLORIDENTIFIER_H