// Copyright (c) 2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef TAPYRUS_COLORIDENTIFIER_H
#define TAPYRUS_COLORIDENTIFIER_H

#include <crypto/sha256.h>
#include <streams.h>
#include <version.h>

enum class TokenTypes
{
    NONE = 0, //TPC
    REISSUABLE = 1,
    NON_REISSUABLE = 2,
    NFT = 3,
    TOKENTYPE_MAX = NFT
};

inline uint8_t TokenToUint(TokenTypes t)
{
    switch(t)
    {
        case TokenTypes::NONE: return 0;
        case TokenTypes::REISSUABLE: return 1;
        case TokenTypes::NON_REISSUABLE: return 2;
        case TokenTypes::NFT: return 3;
        default: return 0;
    }
}

inline TokenTypes UintToToken(uint8_t t)
{
    switch(t)
    {
        case 0: return TokenTypes::NONE;
        case 1: return TokenTypes::REISSUABLE;
        case 2: return TokenTypes::NON_REISSUABLE;
        case 3: return TokenTypes::NFT;
        default: return TokenTypes::NONE;
    }
}

struct ColorIdentifier
{
    TokenTypes type;
    union ColorIdentifierPayload{
        uint8_t scripthash[CSHA256::OUTPUT_SIZE];
        COutPoint utxo;
        ColorIdentifierPayload(){}
    } payload;

    ColorIdentifier(const TokenTypes intype):type(intype) { }

    ColorIdentifier(COutPoint &utxoIn, TokenTypes typeIn)
    {
        if(typeIn == TokenTypes::NON_REISSUABLE || typeIn == TokenTypes::NFT)
        {
            type = typeIn;
            payload.utxo = utxoIn;
        }
    }

    ColorIdentifier(const CScript& input):type(TokenTypes::REISSUABLE)
    {
        std::vector<unsigned char> scriptVector(input.begin(), input.end());
        CSHA256().Write(scriptVector.data(), scriptVector.size()).Finalize(payload.scripthash);
    }

    ColorIdentifier(const std::vector<unsigned char>& in) {
        CSerActionSerialize ser_action;
        CDataStream s(in, SER_NETWORK, INIT_PROTO_VERSION);
        SerializationOp(s, ser_action);
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

        if(this->type == TokenTypes::REISSUABLE)
            READWRITE(this->payload.scripthash);
        else if(this->type == TokenTypes::NON_REISSUABLE || this->type == TokenTypes::NFT)
            READWRITE(this->payload.utxo);
    }

    inline std::vector<unsigned char> toVector()
    {
        CDataStream stream(SER_NETWORK, INIT_PROTO_VERSION);
        this->Serialize(stream);
        return std::vector<unsigned char>(stream.begin(), stream.end());
    }

};

ColorIdentifier GetColorIdFromScript(const CScript& script);

#endif //TAPYRUS_COLORIDENTIFIER_H