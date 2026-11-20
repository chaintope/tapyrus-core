// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coinsviews.h>

#include <chainnotifications.h>
#include <txdb.h>

#include <stdexcept>

bool CCoinsViewErrorCatcher::GetCoin(const COutPoint &outpoint, Coin &coin) const
{
    try {
        return CCoinsViewBacked::GetCoin(outpoint, coin);
    } catch(const std::runtime_error& e) {
        m_notifications.ReportFatalError(FatalErrorReason::COINS_READ_FAILED, e.what());
        // Starting the shutdown sequence and returning false to the caller would be
        // interpreted as 'entry not found' (as opposed to unable to read data), and
        // could lead to invalid interpretation. Just exit immediately, as we can't
        // continue anyway, and all writes should be atomic.
        abort();
    }
}

CoinsViews::CoinsViews(CIssuedColorIds& issuedColorIds, size_t cache_size_bytes, bool in_memory, bool should_wipe, ChainNotifications& notifications)
    : m_dbview(std::make_unique<CCoinsViewDB>(issuedColorIds, cache_size_bytes, in_memory, should_wipe)),
      m_catcherview(m_dbview.get(), notifications)
{
}

CoinsViews::~CoinsViews() = default;

void CoinsViews::InitCache()
{
    AssertLockHeld(cs_main);
    m_cacheview = std::make_unique<CCoinsViewCache>(&m_catcherview);
}
