// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2018-2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_MESSAGEMAGIC_H
#define TAPYRUS_MESSAGEMAGIC_H

#include <string>

/** Text prepended to a message before it is hashed for signmessage and
 *  verifymessage. */
extern const std::string MESSAGE_MAGIC;

#endif // TAPYRUS_MESSAGEMAGIC_H
