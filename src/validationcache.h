// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_VALIDATIONCACHE_H
#define TAPYRUS_VALIDATIONCACHE_H

#include <cs_main.h>
#include <cuckoocache.h>
#include <random.h>
#include <script/sigcache.h>
#include <sync.h>
#include <uint256.h>

#include <cstddef>

/**
 * The caches that let a transaction's scripts be checked once, when it enters
 * the mempool, and not again when its block connects, as Core's
 * ValidationCache. ChainstateManager owns it; CheckInputs takes it.
 */
class ValidationCache
{
public:
    /** Sizes each cache to at most bytes and empties both. */
    void Resize(size_t bytes) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Signatures already verified, keyed by signature hash, public key and signature. */
    SignatureCache& Signatures() { return m_signature_cache; }

    /** The script execution cache key for a transaction checked with the given flags. */
    uint256 ScriptExecutionEntry(const uint256& txid, unsigned int flags) const;
    /** Whether all of a transaction's scripts passed with these flags; erase removes the entry. */
    bool HasScriptExecution(const uint256& entry, bool erase) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void AddScriptExecution(const uint256& entry) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    SignatureCache m_signature_cache;
    //! Random, so peers cannot predict which entries collide.
    const uint256 m_script_execution_nonce{GetRandHash()};
    //! CuckooCache has no lock of its own.
    CuckooCache::cache<uint256, SignatureCacheHasher> m_script_execution_cache GUARDED_BY(cs_main);
};

#endif // TAPYRUS_VALIDATIONCACHE_H
