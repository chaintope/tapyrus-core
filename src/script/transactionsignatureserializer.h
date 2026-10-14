// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_SCRIPT_TRANSACTIONSIGNATURESERIALIZER_H
#define TAPYRUS_SCRIPT_TRANSACTIONSIGNATURESERIALIZER_H

#include <amount.h>

class CScript;
class uint256;

template <class T>
uint256 SignatureHash(const CScript& scriptCode, const T& txTo, unsigned int nIn, int nHashType, const CAmount& amount);

#endif // TAPYRUS_SCRIPT_TRANSACTIONSIGNATURESERIALIZER_H
