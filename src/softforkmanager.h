// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_SOFTFORKMANAGER_H
#define TAPYRUS_SOFTFORKMANAGER_H

#include <cstdint>
#include <functional>
#include <vector>

/**
 * Activation predicate for height-based softforks.
 * Returns true once blockHeight reaches activationHeight.
 */
struct HeightActivation {
    int32_t activationHeight;
    explicit HeightActivation(int32_t h) : activationHeight(h) {}
    bool operator()(int32_t blockHeight) const { return blockHeight >= activationHeight; }
};

/**
 * Represents a single softfork rule tied to a specific Tapyrus network.
 *
 * networkId        — the Tapyrus networkId this fork applies to
 * scriptVerifyFlag — SCRIPT_VERIFY_* bit(s) added to GetBlockScriptFlags()
 *                    when the fork is active
 * activationHeight — block height at which the fork becomes active (for introspection)
 * isActive         — callable(blockHeight) → bool; the canonical activation check;
 *                    typically a HeightActivation but may encode any predicate
 */
struct CSoftFork {
    uint32_t networkId;
    unsigned int scriptVerifyFlag;
    int32_t activationHeight;
    std::function<bool(int32_t blockHeight)> isActive;

    CSoftFork(uint32_t netId, unsigned int flag, HeightActivation activation)
        : networkId(netId), scriptVerifyFlag(flag),
          activationHeight(activation.activationHeight), isActive(std::move(activation)) {}
};

/**
 * Manages all registered softforks across networks.
 *
 * Populated once at startup in PROD mode via CFederationParams.
 * Empty in DEV mode — no forks are enforced.
 * Thread-safe for reads after startup (no concurrent writes expected).
 */
class CSoftForkManager {
    std::vector<CSoftFork> m_softforks;

public:
    void Register(CSoftFork sf) { m_softforks.push_back(std::move(sf)); }

    /**
     * Returns the registered CSoftFork for the given scriptVerifyFlag on networkId,
     * or nullptr if no entry exists (i.e. the fork is active from genesis on that network).
     */
    const CSoftFork* GetFork(uint32_t networkId, unsigned int flag) const {
        for (const auto& sf : m_softforks)
            if (sf.networkId == networkId && (sf.scriptVerifyFlag & flag))
                return &sf;
        return nullptr;
    }

    /**
     * Returns the union of SCRIPT_VERIFY_* flags for all softforks that are
     * active on networkId at blockHeight.
     */
    unsigned int GetScriptFlags(uint32_t networkId, int32_t blockHeight) const {
        unsigned int flags = 0;
        for (const auto& sf : m_softforks) {
            if (sf.networkId == networkId && sf.isActive(blockHeight))
                flags |= sf.scriptVerifyFlag;
        }
        return flags;
    }

    /**
     * Returns whether the given flag is active for networkId at blockHeight.
     *
     * When no entry exists for (networkId, flag) the softfork is considered
     * active from genesis — the manager stores only networks that need a
     * non-zero activation height.  Callers that want the genesis-default
     * behaviour for unregistered networks rely on this returning true.
     */
    bool IsActive(uint32_t networkId, unsigned int flag, int32_t blockHeight) const {
        for (const auto& sf : m_softforks)
            if (sf.networkId == networkId && (sf.scriptVerifyFlag & flag))
                return sf.isActive(blockHeight);
        return true;  // no entry → active from genesis by default
    }

    /**
     * Convenience overload: queries the current network via FederationParams().
     * Implemented in federationparams.cpp to avoid a circular include dependency.
     */
    bool IsActive(unsigned int flag, int32_t blockHeight) const;

    /**
     * Returns whether the softfork flag is reflected in the given pre-computed
     * script-verify flags.  Used in the script interpreter, which sees only the
     * flags bitmask (already produced by GetBlockScriptFlags / IsActive) and
     * cannot query by block height.
     */
    bool IsEnabled(unsigned int flag, unsigned int scriptFlags) const {
        return (scriptFlags & flag) != 0;
    }

};

/**
 * Returns the softfork manager for the currently active network.
 * Declared here so that script/interpreter.cpp can call it without
 * including federationparams.h (which would create a circular dependency).
 * Implemented in federationparams.cpp.
 */
const CSoftForkManager& GetSoftForkManager();

#endif // TAPYRUS_SOFTFORKMANAGER_H
