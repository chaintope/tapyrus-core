// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/outpoint.h>

#include <tinyformat.h>

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hashMalFix.ToString().substr(0,10), n);
}
