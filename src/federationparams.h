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
#include <softforkmanager.h>
#include <util.h>

// Activation height for SCRIPT_VERIFY_CP2SH_COLORED on Chaintope testnet
// (networkId 1939510133). The last legacy CP2SH colored spend on testnet was
// at block 693366
static constexpr int TESTNET_CP2SH_ACTIVATION_HEIGHT = 693367;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};


/**
 * CFederationParams defines the base parameters (shared between bitcoin-cli and bitcoind)
 * of a given instance of the Bitcoin system.
 */
class CFederationParams
{
public:
    std::string NetworkIDString() const { return strNetworkID; }
    uint32_t NetworkId() const { return nNetworkId; }
    const CMessageHeader::MessageStartChars& MessageStart() const { return pchMessageStart; }
    const std::string& getDataDir() const { return dataDir; }
    /** Return the list of hostnames to look up for DNS seeds */
    const std::vector<std::string>& DNSSeeds() const { return vSeeds; }

    /** Returns the softfork manager holding all registered softforks for this node's network. */
    const CSoftForkManager& SoftForkManager() const { return m_softForkManager; }

    CFederationParams();
    CFederationParams(const uint32_t networkId, const std::string dataDirName);

    /** Register a softfork; called once at startup from CreateFederationParams (PROD mode only). */
    void RegisterSoftFork(CSoftFork sf) { m_softForkManager.Register(std::move(sf)); }

    friend std::unique_ptr<CFederationParams> CreateFederationParams(const TAPYRUS_OP_MODE mode);

private:
    uint32_t nNetworkId;
    CSoftForkManager m_softForkManager;
    CMessageHeader::MessageStartChars pchMessageStart;
    std::string strNetworkID;
    std::string dataDir;
    std::vector<std::string> vSeeds;
};

/**
 * Creates and returns a std::unique_ptr<CFederationParams> of the chosen chain.
 * @returns a CFederationParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::unique_ptr<CFederationParams> CreateFederationParams(const TAPYRUS_OP_MODE mode);

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
void SelectFederationParams(const TAPYRUS_OP_MODE mode);

#endif // BITCOIN_FEDERATIONPARAMS_H
