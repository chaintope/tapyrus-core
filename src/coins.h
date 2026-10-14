// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2019-2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINS_H
#define BITCOIN_COINS_H

#include <coin.h>
#include <hash.h>
#include <memusage.h>
#include <primitives/outpoint.h>
#include <uint256.h>

#include <assert.h>
#include <stdint.h>

#include <support/allocators/pool.h>

#include <unordered_map>

class SaltedOutpointHasher
{
private:
    /** Salt */
    const uint64_t k0, k1;

public:
    SaltedOutpointHasher();

    /**
     * This *must* return size_t. With Boost 1.46 on 32-bit systems the
     * unordered_map will behave unpredictably if the custom hasher returns a
     * uint64_t, resulting in failures when syncing the chain (#4634).
     */
    size_t operator()(const COutPoint& id) const {
        return SipHashUint256Extra(k0, k1, id.hashMalFix, id.n);
    }
};

// Forward-declare CCoinsCacheEntry so CoinsCachePair can be defined before
// CCoinsCacheEntry is complete. The linked-list pointers in CCoinsCacheEntry
// are pointer types that only require CoinsCachePair to be declared, not complete.
struct CCoinsCacheEntry;
using CoinsCachePair = std::pair<const COutPoint, CCoinsCacheEntry>;

/**
 * A Coin in one level of the coins database caching hierarchy.
 *
 * A coin can either be:
 * - unspent or spent (in which case the Coin object will be nulled out - see Coin.Clear())
 * - DIRTY or not DIRTY
 * - FRESH or not FRESH
 */
struct CCoinsCacheEntry
{
private:
    /**
     * These are used to create a doubly linked list of flagged entries.
     * They are set in AddFlags and unset in ClearFlags.
     * A flagged entry is any entry that is either DIRTY, FRESH, or both.
     *
     * DIRTY entries are tracked so that only modified entries can be passed to
     * the parent cache for batch writing. This is a performance optimization
     * compared to iterating the entire cache to find modified entries.
     */
    CoinsCachePair* m_prev{nullptr};
    CoinsCachePair* m_next{nullptr};
    uint8_t m_flags{0};

public:
    Coin coin; // The actual cached data.

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
        FRESH = (1 << 1), // The parent view does not have this entry (or it is pruned).
        /* Note that FRESH is a performance optimization with which we can
         * erase coins that are fully spent if we know we do not need to
         * flush the changes to the parent cache.  It is always safe to
         * not mark FRESH if that condition is not guaranteed.
         */
    };

    CCoinsCacheEntry() noexcept = default;
    explicit CCoinsCacheEntry(Coin&& coin_) noexcept : coin(std::move(coin_)) {}

    CCoinsCacheEntry(const CCoinsCacheEntry&) = delete;
    CCoinsCacheEntry& operator=(const CCoinsCacheEntry&) = delete;
    CCoinsCacheEntry(CCoinsCacheEntry&&) = delete;
    CCoinsCacheEntry& operator=(CCoinsCacheEntry&&) = delete;

    ~CCoinsCacheEntry()
    {
        ClearFlags();
    }

    //! Add flags and, if this is the first time any flags are set, insert
    //! this entry into the doubly linked list of flagged entries.
    //! Requires a reference to the CoinsCachePair containing this entry (self)
    //! and the sentinel of the linked list (sentinel).
    inline void AddFlags(uint8_t flags, CoinsCachePair& self, CoinsCachePair& sentinel) noexcept
    {
        assert(&self.second == this);
        if (!m_flags && flags) {
            // Insert at the tail of the list (just before the sentinel).
            m_prev = sentinel.second.m_prev;
            m_next = &sentinel;
            sentinel.second.m_prev = &self;
            m_prev->second.m_next = &self;
        }
        m_flags |= flags;
    }

    //! Clear all flags and remove this entry from the doubly linked list.
    inline void ClearFlags() noexcept
    {
        if (!m_flags) return;
        m_next->second.m_prev = m_prev;
        m_prev->second.m_next = m_next;
        m_flags = 0;
    }

    inline uint8_t GetFlags() const noexcept { return m_flags; }
    inline bool IsDirty() const noexcept { return m_flags & DIRTY; }
    inline bool IsFresh() const noexcept { return m_flags & FRESH; }

    //! Only call Next when this entry is DIRTY, FRESH, or both.
    inline CoinsCachePair* Next() const noexcept {
        assert(m_flags);
        return m_next;
    }

    //! Only call Prev when this entry is DIRTY, FRESH, or both.
    inline CoinsCachePair* Prev() const noexcept {
        assert(m_flags);
        return m_prev;
    }

    //! Only use this for initializing the linked list sentinel.
    //! Sets m_prev and m_next to point to self, and sets m_flags to DIRTY
    //! so that Next() can be called on the sentinel.
    inline void SelfRef(CoinsCachePair& self) noexcept
    {
        assert(&self.second == this);
        m_prev = &self;
        m_next = &self;
        m_flags = DIRTY;
    }
};

/**
 * PoolAllocator's MAX_BLOCK_SIZE_BYTES parameter here uses sizeof the data, and adds the size
 * of 4 pointers. We do not know the exact node size used in the std::unordered_map implementation
 * because it is implementation defined. Most implementations have an overhead of 1 or 2 pointers,
 * so nodes can be connected in a linked list, and in some cases the hash value is stored as well.
 * Using an additional sizeof(void*)*4 for MAX_BLOCK_SIZE_BYTES should thus be sufficient so that
 * all implementations can allocate the nodes from the PoolAllocator.
 */
using CCoinsMap = std::unordered_map<COutPoint,
                                     CCoinsCacheEntry,
                                     SaltedOutpointHasher,
                                     std::equal_to<COutPoint>,
                                     PoolAllocator<std::pair<const COutPoint, CCoinsCacheEntry>,
                                                   sizeof(std::pair<const COutPoint, CCoinsCacheEntry>) + sizeof(void*) * 4,
                                                   alignof(void*)>>;

using CCoinsMapMemoryResource = CCoinsMap::allocator_type::ResourceType;

/**
 * Cursor for iterating over the linked list of flagged entries in CCoinsViewCache.
 *
 * This encapsulates the diverging cleanup logic between CCoinsViewCache::Sync
 * (non-erasing: keep the cache warm, only remove spent coins and clear flags)
 * and CCoinsViewCache::Flush (erasing: caller will wipe the entire map).
 *
 * BatchWrite receivers can call WillErase() to decide whether to move or copy
 * the coin out of the entry.
 */
struct CoinsViewCacheCursor
{
    //! If will_erase is false (Sync), iterating clears flags on unspent entries
    //! and erases spent entries from the map.
    //! If will_erase is true (Flush), the map is not touched during iteration;
    //! the caller is expected to call cacheCoins.clear() afterwards.
    CoinsViewCacheCursor(size_t& usage,
                         CoinsCachePair& sentinel,
                         CCoinsMap& map,
                         bool will_erase) noexcept
        : m_usage(usage), m_sentinel(sentinel), m_map(map), m_will_erase(will_erase) {}

    inline CoinsCachePair* Begin() const noexcept { return m_sentinel.second.Next(); }
    inline CoinsCachePair* End()   const noexcept { return &m_sentinel; }

    //! Advance past current, possibly erasing or unflagging it.
    //! Must be called with each entry returned by Begin()/NextAndMaybeErase().
    inline CoinsCachePair* NextAndMaybeErase(CoinsCachePair& current) noexcept
    {
        const auto next_entry{current.second.Next()};
        if (!m_will_erase) {
            if (current.second.coin.IsSpent()) {
                m_usage -= current.second.coin.DynamicMemoryUsage();
                m_map.erase(current.first);
            } else {
                current.second.ClearFlags();
            }
        }
        return next_entry;
    }

    //! Returns true if the caller will erase this entry (either because
    //! will_erase is set, or because the coin is spent and will be erased anyway).
    inline bool WillErase(CoinsCachePair& current) const noexcept
    {
        return m_will_erase || current.second.coin.IsSpent();
    }

private:
    size_t& m_usage;
    CoinsCachePair& m_sentinel;
    CCoinsMap& m_map;
    bool m_will_erase;
};

#endif // BITCOIN_COINS_H
