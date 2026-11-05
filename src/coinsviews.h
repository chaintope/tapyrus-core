// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_COINSVIEWS_H
#define TAPYRUS_COINSVIEWS_H

#include <coinsview.h>
#include <coinsviewcache.h>
#include <cs_main.h>
#include <sync.h>

#include <cstddef>
#include <memory>

class CCoinsViewDB;
class CIssuedColorIds;

/**
 * This is a minimally invasive approach to shutdown on LevelDB read errors from the
 * chainstate, while keeping user interface out of the common library, which is shared
 * between tapyrusd, and tapyrus-qt and non-server tools.
*/
class CCoinsViewErrorCatcher final : public CCoinsViewBacked
{
public:
    explicit CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    // Writes do not need similar protection, as failure to write is handled by the caller.
};

/**
 * The coins database of a chainstate, the read-error catcher over it and the
 * in-memory cache on top, as Core's CoinsViews. The cache is created later than
 * the database, once ReplayBlocks has made the database consistent.
 */
class CoinsViews
{
public:
    CoinsViews(CIssuedColorIds& issuedColorIds, size_t cache_size_bytes, bool in_memory, bool should_wipe);
    //! Defined in the .cpp, so this header needs only a forward declaration of CCoinsViewDB.
    ~CoinsViews();

    void InitCache() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    std::unique_ptr<CCoinsViewDB> m_dbview GUARDED_BY(cs_main);
    CCoinsViewErrorCatcher m_catcherview GUARDED_BY(cs_main);
    std::unique_ptr<CCoinsViewCache> m_cacheview GUARDED_BY(cs_main);
};

#endif // TAPYRUS_COINSVIEWS_H
