// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_OUTPUTTYPE_H
#define BITCOIN_OUTPUTTYPE_H

#include <keystore.h>
#include <script/standard.h>

#include <string>
#include <vector>

enum class OutputType {
    LEGACY,
    TOKEN,
    CHANGE_AUTO,
};

/**
 * Get a destination of the requested type (if possible) to the specified key.
 */
CTxDestination GetDestinationForKey(const CPubKey& key, OutputType, const ColorIdentifier& colorId);

/** Get all destinations (potentially) supported by the wallet for the given key. */
std::vector<CTxDestination> GetAllDestinationsForKey(const CPubKey& key);

/**
 * Get a destination of the requested type (if possible) to the specified script.
 * This function will automatically add the script (and any other
 * necessary scripts) to the keystore.
 */
CTxDestination AddAndGetDestinationForScript(CKeyStore& keystore, const CScript& script, OutputType, const ColorIdentifier& colorId);

#endif // BITCOIN_OUTPUTTYPE_H

