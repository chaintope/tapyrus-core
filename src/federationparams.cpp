// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <federationparams.h>

#include <tinyformat.h>
#include <util.h>
#include <utilmemory.h>
#include <utilstrencodings.h>
#include <tapyrusmodes.h>

#include <assert.h>
#include <stdexcept>

void SetupFederationParamsOptions()
{
    gArgs.AddArg("-dev", "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                                   "This is intended for regression testing tools and app development.", true, OptionsCategory::CHAINPARAMS);
}

static std::unique_ptr<CFederationParams> globalChainFederationParams;

const CFederationParams& FederationParams()
{
    assert(globalChainFederationParams);
    return *globalChainFederationParams;
}

std::unique_ptr<CFederationParams> CreateFederationParams(const TAPYRUS_OP_MODE mode)
{
    gArgs.SelectConfigNetwork(TAPYRUS_MODES::GetChainName(mode));

    int64_t nid;
    bool inRange = gArgs.IsGetArgInRange("-networkid", 1, UINT_MAX,  TAPYRUS_MODES::GetDefaultNetworkId(mode), nid);
    if(!inRange || nid <= 0)
        throw std::runtime_error(strprintf("Network Id [%ld] was out of range. Expected range is 1 to 4294967295.", nid));
    const uint32_t networkId = (uint32_t)nid;
    const std::string dataDirName(GetDataDirNameFromNetworkId(networkId));

    return MakeUnique<CFederationParams>(networkId, dataDirName);
}

void SelectFederationParams(const TAPYRUS_OP_MODE mode)
{
    globalChainFederationParams = CreateFederationParams(mode);
}

CFederationParams::CFederationParams(const uint32_t networkId, const std::string dataDirName) : nNetworkId(networkId), strNetworkID(std::to_string(networkId)), dataDir(dataDirName) {

    /**
     * The message start string is designed to be unlikely to occur in normal data.
     * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
     * a large 32-bit integer with any alignment.
     *
     * tapyrus message start string is 0x01 0xFF 0xF0 0x00.
     * testnet message start string is 0x75 0x9A 0x83 0x74. it is xor of mainnet header and testnet ascii codes.
     */

    uint32_t magicBytes = 33550335u + nNetworkId;
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << magicBytes;
    pchMessageStart[0] = stream[3];
    pchMessageStart[1] = stream[2];
    pchMessageStart[2] = stream[1];
    pchMessageStart[3] = stream[0];

    vSeeds = gArgs.GetArgs("-addseeder");
}

