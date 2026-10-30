// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AMOUNT_H
#define BITCOIN_AMOUNT_H

#include <consensusconstants.h>

#include <stdint.h>

/** Amount in tapyrus (Can be negative) */
typedef int64_t CAmount;

static const CAmount CENT = 1000000;

inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= ConsensusConstants::MAX_MONEY); }

#endif //  BITCOIN_AMOUNT_H
