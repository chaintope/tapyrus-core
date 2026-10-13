// Copyright (c) 2020-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef TAPYRUS_TOKENTYPES_H
#define TAPYRUS_TOKENTYPES_H

#include <stdint.h>

enum class TokenTypes : uint8_t
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

#endif // TAPYRUS_TOKENTYPES_H
