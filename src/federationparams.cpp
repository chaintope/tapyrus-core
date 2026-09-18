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
#include <script/interpreter.h>

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

    auto params = MakeUnique<CFederationParams>(networkId, dataDirName);

    // CP2SH colored softfork registration:
    //
    // CI test builds define CP2SH_ACTIVATION_TEST_HEIGHT so the functional test
    // can cross the activation boundary without mining hundreds of thousands of
    // blocks.  The override applies to whichever network the test node is running.
    //
    // Production builds register only the Chaintope testnet (1939510133) entry,
    // deferring activation to TESTNET_CP2SH_ACTIVATION_HEIGHT
    // All other networks have no entry and therefore activate CP2SH at genesis
    // (CSoftForkManager::IsActive returns true when no entry is found).
#ifdef CP2SH_ACTIVATION_TEST_HEIGHT
    params->RegisterSoftFork(CSoftFork(
        networkId, SCRIPT_VERIFY_CP2SH_COLORED,
        HeightActivation(CP2SH_ACTIVATION_TEST_HEIGHT)
    ));
#else
    if (networkId == 1939510133u) {
        params->RegisterSoftFork(CSoftFork(
            networkId, SCRIPT_VERIFY_CP2SH_COLORED,
            HeightActivation(TESTNET_CP2SH_ACTIVATION_HEIGHT)
        ));
    }
#endif

    return params;
}

void SelectFederationParams(const TAPYRUS_OP_MODE mode)
{
    globalChainFederationParams = CreateFederationParams(mode);
}

const CSoftForkManager& GetSoftForkManager()
{
    return FederationParams().SoftForkManager();
}

bool CSoftForkManager::IsActive(unsigned int flag, int32_t blockHeight) const
{
    return IsActive(FederationParams().NetworkId(), flag, blockHeight);
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

