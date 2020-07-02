// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2018-2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <tinyformat.h>
#include <util.h>
#include <assert.h>

#include <streams.h>
#include <fs.h>
#include <utilstrencodings.h>
#include <tapyrusmodes.h>
/**
 * Production network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CProductionChainParams : public CChainParams {
public:
    CProductionChainParams():CChainParams() {
        consensus.nSubsidyHalvingInterval = 210000;

        consensus.nExpectedBlockTime = 15; // 15 sec
        rpcPort = 2377;
        nDefaultPort = 2357;
        nPruneAfterHeight = 100000;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};
        base58Prefixes[C_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0x16);
        base58Prefixes[C_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,0x18);

        fDefaultConsistencyChecks = false;
        fMineBlocksOnDemand = false;

        /* disable fallback fee on mainnet */
        m_fallback_fee_enabled = false;
    }
};

/**
 * Development test
 */
class CDevelopmentChainParams : public CChainParams {
public:
    CDevelopmentChainParams():CChainParams() {
        consensus.nSubsidyHalvingInterval = 150;

        consensus.nExpectedBlockTime = 15; // 15 sec
        rpcPort = 12381;
        nDefaultPort = 12383;
        nPruneAfterHeight = 1000;

        fDefaultConsistencyChecks = true;
        fMineBlocksOnDemand = true;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        base58Prefixes[C_PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0x36);
        base58Prefixes[C_SCRIPT_ADDRESS] = std::vector<unsigned char>(1,0x38);

        /* enable fallback fee on dev */
        m_fallback_fee_enabled = true;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const TAPYRUS_OP_MODE mode)
{
    switch(mode)
    {
        case TAPYRUS_OP_MODE::PROD:
            return std::unique_ptr<CChainParams>(new CProductionChainParams());
        case TAPYRUS_OP_MODE::DEV:
            return std::unique_ptr<CChainParams>(new CDevelopmentChainParams());
        default:
            throw std::runtime_error(strprintf("%s: Unknown mode.", __func__));
    }
}

void SelectParams(const TAPYRUS_OP_MODE mode)
{
    globalChainParams = CreateChainParams(mode);
}
