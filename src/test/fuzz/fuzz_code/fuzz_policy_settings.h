// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Default policy for fuzz targets that run policy code without a node
// (FuzzNodeSetup's TestingSetup sets its own). Built once per process and
// never destroyed, like GetFuzzNodeSetup(). Targets pass the block size limit
// themselves, ConsensusConstants::MAX_BLOCK_SIZE without a node.

#ifndef TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_POLICY_SETTINGS_H
#define TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_POLICY_SETTINGS_H

#include <policy/policysettings.h>
#include <tapyrusmodes.h>
#include <util.h>

#include <cstdint>

inline const PolicySettings& GetFuzzPolicySettings()
{
    static const PolicySettings* const settings = [] {
        const PolicySettings* const defaults = new PolicySettings(ArgsManager(), TAPYRUS_OP_MODE::PROD);
        g_policy_settings = defaults;
        return defaults;
    }();
    return *settings;
}

#endif // TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_POLICY_SETTINGS_H
