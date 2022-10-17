// Copyright (c) 2014-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FEDERATIONPARAMS_H
#define BITCOIN_FEDERATIONPARAMS_H

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <protocol.h>
#include <streams.h>
#include <pubkey.h>
#include <primitives/block.h>
#include <util.h>

const std::string TAPYRUS_GENESIS_FILENAME = "genesis.dat";

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

struct AggPubkeyAndHeight {
    CPubKey aggpubkey;
    uint32_t height;
};

struct maxBlockSizeAndHeight {
    int64_t maxBlockSize;
    uint64_t height;
};

/**
 * CFederationParams defines the base parameters (shared between bitcoin-cli and bitcoind)
 * of a given instance of the Bitcoin system.
 */
class CFederationParams
{
public:
    std::string NetworkIDString() const { return strNetworkID; }
    const CMessageHeader::MessageStartChars& MessageStart() const { return pchMessageStart; }
    /**
     * Parse aggPubkey in block header.
     */
    CPubKey ReadAggregatePubkey(const std::vector<unsigned char>& pubkey, uint64_t height) const;
    const std::vector<AggPubkeyAndHeight>& GetAggregatePubkeyHeightList() const { return aggregatePubkeyHeight; }
    const CPubKey& GetLatestAggregatePubkey() const { return aggregatePubkeyHeight.back().aggpubkey; }

    int32_t ReadMaxBlockSize(const int32_t maxBlockSize, uint32_t height) const;
    const std::vector<XFieldChange>* GetMaxBlockSizeHeightList() const;
    void RemoveMaxBlockSize(uint32_t height) const;

    bool ReadGenesisBlock(std::string genesisHex);
    const CBlock& GenesisBlock() const { return genesis; }
    const std::string& getDataDir() const { return dataDir; }
    const std::vector<SeedSpec6>& FixedSeeds() const { return vFixedSeeds; }
    /** Return the list of hostnames to look up for DNS seeds */
    const std::vector<std::string>& DNSSeeds() const { return vSeeds; }
    uint32_t GetHeightFromAggregatePubkey(const CPubKey &aggpubkey) const;
    CPubKey GetAggPubkeyFromHeight(uint32_t height) const;
    uint32_t GetMaxBlockSizeFromHeight(uint32_t height) const;

    CFederationParams();
    CFederationParams(const uint32_t networkId, const std::string dataDirName, const std::string genesisHex);

private:
    uint32_t nNetworkId;
    CMessageHeader::MessageStartChars pchMessageStart;
    std::string strNetworkID;
    std::string dataDir;
    mutable std::vector<AggPubkeyAndHeight> aggregatePubkeyHeight;
    CBlock genesis;
    std::vector<std::string> vSeeds;
    std::vector<SeedSpec6> vFixedSeeds;
};

/**
 * Creates and returns a std::unique_ptr<CFederationParams> of the chosen chain.
 * @returns a CFederationParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::unique_ptr<CFederationParams> CreateFederationParams(const TAPYRUS_OP_MODE mode, const bool withGenesis);

/**
 *Set the arguments for chainparams
 */
void SetupFederationParamsOptions();

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CFederationParams& FederationParams();

/** Sets the params returned by Params() to those for the given network. */
void SelectFederationParams(const TAPYRUS_OP_MODE mode, const bool withGenesis=true);

/**
 * Reads the genesis block from genesis.dat into federationparams.
 */
std::string ReadGenesisBlock(fs::path genesisPath = GetDataDir(false));

/**
 * @returns a signed genesis block.
 */
CBlock createGenesisBlock(const CPubKey& aggregatePubkey, const CKey& privateKey, const time_t blockTime=time(0), const std::string paytoAddress="");

#endif // BITCOIN_FEDERATIONPARAMS_H
