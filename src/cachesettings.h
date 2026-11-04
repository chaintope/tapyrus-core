// Copyright (c) 2021 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CACHESETTINGS_H
#define TAPYRUS_CACHESETTINGS_H

#include <cstdint>

class ArgsManager;

//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 450;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache (MiB)
static const int64_t nMinDbCache = 4;
//! Max memory allocated to block tree DB specific cache, if no -txindex (MiB)
static const int64_t nMaxBlockDBCache = 2;
//! Max memory allocated to block tree DB specific cache, if -txindex (MiB)
// Unlike for the UTXO database, for the txindex scenario the leveldb cache make
// a meaningful difference: https://github.com/bitcoin/bitcoin/pull/8273#issuecomment-229601991
static const int64_t nMaxTxIndexCache = 1024;
//! Max memory allocated to coin DB specific cache (MiB)
static const int64_t nMaxCoinsDBCache = 8;
//! -txindex default
static const bool DEFAULT_TXINDEX = false;

/** How -dbcache is split between the databases' own caches and the in-memory coins cache, as Core's CacheSizes. */
class CacheSettings
{
public:
    explicit CacheSettings(const ArgsManager& args);

    int64_t nBlockTreeDBCache;
    int64_t nTxIndexCache;
    int64_t nCoinDBCache;
    //! What is left after the databases: the in-memory coins cache.
    int64_t nCoinCacheUsage;
};

#endif // TAPYRUS_CACHESETTINGS_H
