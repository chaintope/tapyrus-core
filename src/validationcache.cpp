// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validationcache.h>

#include <crypto/sha256.h>
#include <logging.h>

void ValidationCache::Resize(size_t bytes)
{
    size_t nElems = m_signature_cache.setup_bytes(bytes);
    LogPrintf("Using %zu MiB out of %zu/2 requested for signature cache, able to store %zu elements\n",
            (nElems*sizeof(uint256)) >>20, (bytes*2)>>20, nElems);
    nElems = m_script_execution_cache.setup_bytes(bytes);
    LogPrintf("Using %zu MiB out of %zu/2 requested for script execution cache, able to store %zu elements\n",
            (nElems*sizeof(uint256)) >>20, (bytes*2)>>20, nElems);
}

uint256 ValidationCache::ScriptExecutionEntry(const uint256& txid, unsigned int flags) const
{
    uint256 entry;
    // We only use the first 19 bytes of nonce to avoid a second SHA
    // round - giving us 19 + 32 + 4 = 55 bytes (+ 8 + 1 = 64)
    static_assert(55 - sizeof(flags) - 32 >= 128/8, "Want at least 128 bits of nonce for script execution cache");
    CSHA256().Write(m_script_execution_nonce.begin(), 55 - sizeof(flags) - 32).Write(txid.begin(), 32).Write((unsigned char*)&flags, sizeof(flags)).Finalize(entry.begin());
    return entry;
}

bool ValidationCache::HasScriptExecution(const uint256& entry, bool erase)
{
    return m_script_execution_cache.contains(entry, erase);
}

void ValidationCache::AddScriptExecution(const uint256& entry)
{
    m_script_execution_cache.insert(entry);
}
