// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparamsbase.h>

#include <tinyformat.h>
#include <util.h>
#include <utilmemory.h>

#include <assert.h>

const std::string CBaseChainParams::MAIN = "main";
const std::string CBaseChainParams::TESTNET = "test";
const std::string CBaseChainParams::REGTEST = "regtest";

void SetupChainParamsBaseOptions()
{
    gArgs.AddArg("-regtest", "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                                   "This is intended for regression testing tools and app development.", true, OptionsCategory::CHAINPARAMS);
    gArgs.AddArg("-testnet", "Use the test chain", false, OptionsCategory::CHAINPARAMS);

    // Signed Blocks options
    gArgs.AddArg("-signblockpubkeys=<pubkeys>", "Sets the public keys for Signed Blocks multisig that combined as one string.", false, OptionsCategory::SIGN_BLOCK);
    gArgs.AddArg("-signblockthreshold=<n>", "Sets the number of public keys to be the threshold of multisig", false, OptionsCategory::SIGN_BLOCK);
}

bool ParseChainParamsBaseOptionsParameters(int argc, const char* const argv[], std::string& error)
{
    const std::vector<std::string> options({
        "-regtest",
        "-testnet",
        "-signblockpubkeys",
        "-signblockthreshold"
    });

    char const* filteredArgv[5];
    filteredArgv[0] = argv[0];
    int count = 1;

    for (int i = 1; i < argc; i++) {
        std::string key(argv[i]);
        size_t is_index = key.find('=');
        if (is_index != std::string::npos) {
            key.erase(is_index);
        }

        if (std::find(options.begin(), options.end(), key) != options.end())
        {
            filteredArgv[count++] = argv[i];
        }
    }

    if (!gArgs.ParseParameters(count, filteredArgv, error)) {
        return false;
    }

    return true;
}

static std::unique_ptr<CBaseChainParams> globalChainBaseParams;

const CBaseChainParams& BaseParams()
{
    assert(globalChainBaseParams);
    return *globalChainBaseParams;
}

std::unique_ptr<CBaseChainParams> CreateBaseChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return MakeUnique<CBaseChainParams>("", 8332);
    else if (chain == CBaseChainParams::TESTNET)
        return MakeUnique<CBaseChainParams>("testnet3", 18332);
    else if (chain == CBaseChainParams::REGTEST)
        return MakeUnique<CBaseChainParams>("regtest", 18443);
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectBaseParams(const std::string& chain)
{
    globalChainBaseParams = CreateBaseChainParams(chain);
    gArgs.SelectConfigNetwork(chain);
}
