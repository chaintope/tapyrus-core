// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_POLICY_POLICYCONSTANTS_H
#define TAPYRUS_POLICY_POLICYCONSTANTS_H

#include <consensusconstants.h>
#include <script/interpreter.h>

#include <cstdint>

/**
 * The policy values fixed in code. PolicySettings derives from this; signing
 * and the tools include only this header, so they need no settings object.
 */
struct PolicyConstants
{
    /**
     * Standard script verification flags that standard transactions will comply
     * with. However scripts violating these flags may still be present in valid
     * blocks and we must accept those blocks.
     *
     * Note: VerifyScript() no longer bypasses CLEANSTACK for witness-program-shaped
     * scriptPubKeys (OP_0 <push>), so such spends now need CLEANSTACK to pass here,
     * same as any other script. This has no consensus effect (these flags are not
     * used for block validation), but it does tighten mempool/relay standardness:
     * such a spend can no longer be relayed, only mined directly.
     */
    static constexpr uint32_t STANDARD_SCRIPT_VERIFY_FLAGS = SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS |
                                                             SCRIPT_VERIFY_CLEANSTACK |
                                                             SCRIPT_VERIFY_NULLFAIL |
                                                             SCRIPT_VERIFY_CONST_SCRIPTCODE |
                                                             SCRIPT_VERIFY_CP2SH_COLORED;

    /** For convenience, standard but not mandatory verify flags. */
    static constexpr uint32_t STANDARD_NOT_MANDATORY_VERIFY_FLAGS = STANDARD_SCRIPT_VERIFY_FLAGS |
                                                                    SCRIPT_VERIFY_SIGPUSHONLY;

    /** Used as the flags parameter to sequence and nLocktime checks.
     * In Tapyrus this is consensus code as
     * SCRIPT_VERIFY_SIGPUSHONLY allows
     * OP_CHECKSEQUENCEVERIFY and
     * OP_CHECKLOCKTIMEVERIFY to be present in the scriptsig.
     */
    static constexpr uint32_t STANDARD_LOCKTIME_VERIFY_FLAGS = ConsensusConstants::LOCKTIME_VERIFY_SEQUENCE |
                                                               ConsensusConstants::LOCKTIME_MEDIAN_TIME_PAST;
};

#endif // TAPYRUS_POLICY_POLICYCONSTANTS_H
