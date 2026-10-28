// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_GENESISFILE_H
#define TAPYRUS_GENESISFILE_H

#include <fs.h>
#include <util.h>

#include <string>
#include <vector>

const std::string TAPYRUS_GENESIS_FILENAME = "genesis.dat";

/**
 * Reads the genesis block from genesis.dat (genesis.<networkid> with
 * -networkid) and decodes its hex.
 * @throws std::runtime_error if the file cannot be read or is not hex.
 */
std::vector<unsigned char> ReadGenesisBlock(fs::path genesisPath = GetDataDir(false));

#endif // TAPYRUS_GENESISFILE_H
