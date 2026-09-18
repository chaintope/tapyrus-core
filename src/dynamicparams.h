// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DYNAMICPARAMS_H
#define BITCOIN_DYNAMICPARAMS_H

#include <primitives/block.h>
#include <util.h>

#include <memory>
#include <string>

const std::string TAPYRUS_GENESIS_FILENAME = "genesis.dat";

/**
 * Holds the node state that only becomes known after tapyrusd starts:
 * the genesis block (read from disk, or freshly created) and the
 * CXFieldHistory seeded from it. Kept separate from CFederationParams
 * (which holds only static, always-available-without-genesis identity:
 * networkId, message-start bytes, DNS seeds, softfork registration) so
 * that genesis-block parsing/validation, which needs CXFieldHistory, can
 * live at the layer CXFieldHistory itself lives at, without that
 * requirement reaching down into CFederationParams's own layer.
 */
class CDynamicParams
{
public:
    const CBlock& GenesisBlock() const { return genesis; }

    /**
     * Parses and validates genesisHex into a genesis block, then seeds
     * CXFieldHistory from it.
     * @throws a std::runtime_error if genesisHex is not a valid genesis block.
     */
    explicit CDynamicParams(const std::string& genesisHex);

private:
    CBlock genesis;
};

/**
 * Creates and returns a std::unique_ptr<CDynamicParams> parsed from the
 * given genesis hex.
 * @throws a std::runtime_error if the genesis hex is invalid.
 */
std::unique_ptr<CDynamicParams> CreateDynamicParams(const std::string& genesisHex);

/**
 * Return the currently selected dynamic params. This won't change after
 * app startup, except for unit tests.
 */
const CDynamicParams& DynamicParams();

/** Sets the params returned by DynamicParams() from the given genesis hex. */
void SelectDynamicParams(const std::string& genesisHex);

/**
 * Reads the genesis block hex from genesis.dat on disk.
 */
std::string ReadGenesisBlock(fs::path genesisPath = GetDataDir(false));

#endif // BITCOIN_DYNAMICPARAMS_H
