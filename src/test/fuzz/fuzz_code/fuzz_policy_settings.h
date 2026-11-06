// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Default policy for fuzz targets that run policy code without a node
// (FuzzNodeSetup's TestingSetup sets its own). Built once per process and
// never destroyed, like GetFuzzNodeSetup(). Policy reads the latest consensus
// rules, so it also sets fixed ones: a block size limit of 1,000,000 bytes
// and so 20,000 sigops, from a fixed genesis block.

#ifndef TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_POLICY_SETTINGS_H
#define TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_POLICY_SETTINGS_H

#include <chainstatemanager.h>
#include <consensussettings.h>
#include <policy/policysettings.h>
#include <tapyrusmodes.h>
#include <util.h>
#include <utilstrencodings.h>

#include <cstdint>
#include <memory>

// Signed with the unit tests' aggregate key (test_keys_helper.h) at time 1600000000.
inline constexpr char FUZZ_GENESIS_BLOCK_HEX[] = "01000000000000000000000000000000000000000000000000000000000000000000000060a26264bf8f498a194525721244d835be6e66d0dacf3320b70e3c476da8341f26c18219f76e4d0ebddbaa9b744837c2ac65b347673695a23c3cc1a2be4141e100105e5f0121025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3401fc5e2bf88fd53e1a63f7291ff894d19ffca771b8bc10cb37467d6ac2d7c3b411292eefe0c363adf42ae8a82195fec5faf024ca48d91fe97fbd7fac49cdd5f2401010000000100000000000000000000000000000000000000000000000000000000000000000000000000ffffffff0100f2052a010000001976a914834e0737cdb9008db614cd95ec98824e952e3dc588ac00000000";

inline const PolicySettings& GetFuzzPolicySettings()
{
    static const PolicySettings* const settings = [] {
        g_chainstate_manager.InitConsensusSettings(std::make_unique<const ConsensusSettings>(ParseHex(FUZZ_GENESIS_BLOCK_HEX), TAPYRUS_OP_MODE::PROD, uint32_t{1}));
        g_chainstate_manager.InitXFieldHistory(g_chainstate_manager.Settings().GenesisBlock());
        const PolicySettings* const defaults = new PolicySettings(ArgsManager(), TAPYRUS_OP_MODE::PROD);
        g_policy_settings = defaults;
        return defaults;
    }();
    return *settings;
}

#endif // TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_POLICY_SETTINGS_H
