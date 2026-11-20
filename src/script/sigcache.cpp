// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/sigcache.h>

#include <crypto/sha256.h>
#include <pubkey.h>
#include <random.h>
#include <uint256.h>

#include <mutex>
#include <shared_mutex>

SignatureCache::SignatureCache()
{
    GetRandBytes(nonce.begin(), 32);
}

void SignatureCache::ComputeEntry(uint256& entry, const uint256& hash, const std::vector<unsigned char>& vchSig, const CPubKey& pubkey) const
{
    CSHA256().Write(nonce.begin(), 32).Write(hash.begin(), 32).Write(&pubkey[0], pubkey.size()).Write(&vchSig[0], vchSig.size()).Finalize(entry.begin());
}

bool SignatureCache::Get(const uint256& entry, const bool erase)
{
    std::shared_lock<std::shared_mutex> lock(cs_sigcache);
    return setValid.contains(entry, erase);
}

void SignatureCache::Set(const uint256& entry)
{
    std::unique_lock<std::shared_mutex> lock(cs_sigcache);
    setValid.insert(entry);
}

uint32_t SignatureCache::setup_bytes(size_t n)
{
    std::unique_lock<std::shared_mutex> lock(cs_sigcache);
    return setValid.setup_bytes(n);
}

bool CachingTransactionSignatureChecker::VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& pubkey, const uint256& sighash) const
{
    uint256 entry;
    m_signature_cache.ComputeEntry(entry, sighash, vchSig, pubkey);
    if (m_signature_cache.Get(entry, !store))
        return true;
    if (!TransactionSignatureChecker::VerifySignature(vchSig, pubkey, sighash))
        return false;
    if (store)
        m_signature_cache.Set(entry);
    return true;
}
