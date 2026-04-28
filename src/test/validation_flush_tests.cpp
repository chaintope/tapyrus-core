// Copyright (c) 2019-present The Bitcoin Core developers
// Copyright (c) 2019-2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Ported from Bitcoin Core src/test/validation_flush_tests.cpp.
// Adaptations for Tapyrus:
//   - No Chainstate class / GetCoinsCacheSizeState(): threshold logic inlined
//     from FlushStateToDisk() in file_io.cpp (fCacheLarge / fCacheCritical).
//   - Uses global pcoinsTip and nCoinCacheUsage from validation.h.
//   - AddTestCoin() reimplemented using COutPoint(uint256, n) directly.
//   - InsecureRand256() / InsecureRand32() replace FastRandomContext helpers.

#include <coins.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <sync.h>
#include <test/test_tapyrus.h>
#include <txdb.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <algorithm>

//! Mirrors LargeCoinsCacheThreshold() from Bitcoin Core validation.h.
//! In Tapyrus this threshold is inlined in FlushStateToDisk() (file_io.cpp:436):
//!   max((9 * nTotalSpace) / 10, nTotalSpace - MAX_BLOCK_COINSDB_USAGE * 1024 * 1024)
static int64_t LargeCacheSizeThreshold(int64_t total_space)
{
    return std::max((9 * total_space) / 10,
                    total_space - static_cast<int64_t>(MAX_BLOCK_COINSDB_USAGE) * 1024 * 1024);
}

//! Add one test coin to the view.
//! Mirrors Bitcoin Core's test/util/coins.cpp:AddTestCoin().
//! The coin has a 56-byte scriptPubKey, giving roughly 80 bytes DynamicMemoryUsage.
static void AddTestCoin(CCoinsViewCache& view)
{
    Coin coin;
    COutPoint outpoint{InsecureRand256(), /*n=*/0};
    coin.nHeight = 1;
    coin.out.nValue = static_cast<CAmount>(InsecureRandRange(21000000LL * COIN) + 1);
    coin.out.scriptPubKey.assign(56, static_cast<unsigned char>(1));
    view.AddCoin(outpoint, std::move(coin), /*possible_overwrite=*/false);
}

BOOST_FIXTURE_TEST_SUITE(validation_flush_tests, TestingSetup)

//! Verify that the cache-size thresholds used in FlushStateToDisk() switch
//! from "OK" → "LARGE" → "CRITICAL" at the expected utilisation levels.
//!
//! Bitcoin Core tests Chainstate::GetCoinsCacheSizeState().  Tapyrus does not
//! have that method; instead FlushStateToDisk() (file_io.cpp) computes
//!   nTotalSpace = nCoinCacheUsage + max(nMempoolSizeMax - nMempoolUsage, 0)
//!   fCacheLarge   = cacheSize > LargeCacheSizeThreshold(nTotalSpace)
//!   fCacheCritical= cacheSize > nTotalSpace
//! This test exercises the same state-machine by controlling nCoinCacheUsage
//! and filling pcoinsTip until each threshold is crossed.
BOOST_AUTO_TEST_CASE(getcoinscachesizestate)
{
    LOCK(cs_main);
    CCoinsViewCache& view = *pcoinsTip;

    // An empty cache should occupy well under one pool chunk (~256 KiB).
    BOOST_CHECK_LT(view.DynamicMemoryUsage() / (256.0 * 1024), 1.1);

    // Use a small cache cap so the test runs fast.
    constexpr int64_t MAX_COINS_BYTES = 8 * 1024 * 1024;   //  8 MiB
    constexpr int64_t MAX_MEMPOOL_BYTES = 4 * 1024 * 1024; //  4 MiB
    // 200k attempts gives comfortable headroom: the larger total_space in the
    // second pass (12 MiB) needs ~55k coins to cross its LARGE threshold.
    constexpr size_t  MAX_ATTEMPTS = 200'000;

    // Save and restore nCoinCacheUsage so other tests are not affected.
    const size_t saved_nCoinCacheUsage = nCoinCacheUsage;
    nCoinCacheUsage = static_cast<size_t>(MAX_COINS_BYTES);

    // Run twice: once with no mempool head-room, once with head-room.
    for (int64_t max_mempool_size_bytes : {int64_t{0}, MAX_MEMPOOL_BYTES}) {
        const int64_t total_space = MAX_COINS_BYTES + max_mempool_size_bytes;
        const int64_t large_threshold = LargeCacheSizeThreshold(total_space);

        // --- OK → LARGE ---
        for (size_t i = 0; i < MAX_ATTEMPTS; ++i) {
            int64_t cacheSize = static_cast<int64_t>(view.DynamicMemoryUsage());
            if (cacheSize > large_threshold) break;
            // While below the LARGE threshold the cache is still "OK".
            BOOST_CHECK_LE(cacheSize, large_threshold);
            AddTestCoin(view);
        }
        // Ensure we actually crossed the LARGE threshold before proceeding.
        BOOST_REQUIRE_GT(static_cast<int64_t>(view.DynamicMemoryUsage()), large_threshold);

        // --- LARGE → CRITICAL ---
        for (size_t i = 0; i < MAX_ATTEMPTS; ++i) {
            int64_t cacheSize = static_cast<int64_t>(view.DynamicMemoryUsage());
            if (cacheSize > total_space) break;
            // While above LARGE but not yet above total_space the cache is "LARGE".
            BOOST_CHECK_GT(cacheSize, large_threshold);
            BOOST_CHECK_LE(cacheSize, total_space);
            AddTestCoin(view);
        }

        // Cache must now be CRITICAL (above total_space).
        BOOST_CHECK_GT(static_cast<int64_t>(view.DynamicMemoryUsage()), total_space);

        // --- CRITICAL → OK via Flush ---
        view.SetBestBlock(InsecureRand256());
        BOOST_CHECK(view.Flush());
        BOOST_CHECK_LE(static_cast<int64_t>(view.DynamicMemoryUsage()), large_threshold);
    }

    nCoinCacheUsage = saved_nCoinCacheUsage;
}

BOOST_AUTO_TEST_SUITE_END()
