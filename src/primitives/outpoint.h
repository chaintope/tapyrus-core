// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_PRIMITIVES_OUTPOINT_H
#define TAPYRUS_PRIMITIVES_OUTPOINT_H

#include <serialize.h>
#include <uint256.h>

#include <stdint.h>
#include <string>

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    //tapyrus outpoint uses hashMalFix of previous transaction. So renamed this variable for clarity
    uint256 hashMalFix;
    uint32_t n;

    COutPoint(): n((uint32_t) -1) { }
    COutPoint(const uint256& hashIn, uint32_t nIn): hashMalFix(hashIn), n(nIn) { }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hashMalFix);
        READWRITE(n);
    }

    void SetNull() { hashMalFix.SetNull(); n = (uint32_t) -1; }
    bool IsNull() const { return (hashMalFix.IsNull() && n == (uint32_t) -1); }

    friend bool operator<(const COutPoint& a, const COutPoint& b)
    {
        int cmp = a.hashMalFix.Compare(b.hashMalFix);
        return cmp < 0 || (cmp == 0 && a.n < b.n);
    }

    friend bool operator==(const COutPoint& a, const COutPoint& b)
    {
        return (a.hashMalFix == b.hashMalFix && a.n == b.n);
    }

    friend bool operator!=(const COutPoint& a, const COutPoint& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

#endif // TAPYRUS_PRIMITIVES_OUTPOINT_H
