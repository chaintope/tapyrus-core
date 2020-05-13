// Copyright (c) 2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_COLOR_IDENTIFIER_H
#define BITCOIN_COLOR_IDENTIFIER_H

#include <crypto/sha256.h>

enum class TokenTypes
{
    NONE = 0, //TPC
    REISSUABLE = 1,
    NON_REISSUABLE = 2,
    NFT = 3,
    TOKENTYPE_MAX = NFT
};

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

    ColorIdentifier(CScript& input):type(TokenTypes::REISSUABLE)
    {
        const std::vector<const unsigned char> scriptVector(input.begin(), input.end());
        CSHA256().Write(scriptVector.data(), scriptVector.size()).Finalize(payload.scripthash);
    }
};
#endif //BITCOIN_COLOR_IDENTIFIER_H