// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2019-2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINS_H
#define BITCOIN_COINS_H

#include <primitives/transaction.h>
#include <compressor.h>
#include <core_memusage.h>
#include <hash.h>
#include <memusage.h>
#include <serialize.h>
#include <uint256.h>
#include <coloridentifier.h>

#include <assert.h>
#include <stdint.h>

#include <support/allocators/pool.h>

#include <unordered_map>

/**
 * A UTXO entry.
 *
 * Serialized format:
 * - VARINT((coinbase ? 1 : 0) | (height << 1))
 * - the non-spent CTxOut (via CTxOutCompressor)
 */
class Coin
{
public:
    //! unspent transaction output
    CTxOut out;

    //! whether containing transaction was a coinbase
    unsigned int fCoinBase : 1;

    //! at which height this containing transaction was included in the active block chain
    uint32_t nHeight : 31;

    //! construct a Coin from a CTxOut and height/coinbase information.
    Coin(CTxOut&& outIn, int nHeightIn, bool fCoinBaseIn) : out(std::move(outIn)), fCoinBase(fCoinBaseIn), nHeight(nHeightIn) {}
    Coin(const CTxOut& outIn, int nHeightIn, bool fCoinBaseIn) : out(outIn), fCoinBase(fCoinBaseIn),nHeight(nHeightIn){}

    void Clear() {
        out.SetNull();
        fCoinBase = false;
        nHeight = 0;
    }

    //! empty constructor
    Coin() : fCoinBase(false), nHeight(0) { }

    bool IsCoinBase() const {
        return fCoinBase;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        assert(!IsSpent());
        uint32_t code = nHeight * 2 + fCoinBase;
        ::Serialize(s, VARINT(code));
        ::Serialize(s, CTxOutCompressor(REF(out)));
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        uint32_t code = 0;
        ::Unserialize(s, VARINT(code));
        nHeight = code >> 1;
        fCoinBase = code & 1;
        ::Unserialize(s, CTxOutCompressor(out));
    }

    bool IsSpent() const {
        return out.IsNull();
    }

    size_t DynamicMemoryUsage() const {
        return memusage::DynamicUsage(out.scriptPubKey);
    }
};

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

/** Cursor for iterating over CoinsView state */
class CCoinsViewCursor
{
public:
    CCoinsViewCursor(const uint256 &hashBlockIn): hashBlock(hashBlockIn) {}
    virtual ~CCoinsViewCursor() {}

    virtual bool GetKey(COutPoint &key) const = 0;
    virtual bool GetValue(Coin &coin) const = 0;
    virtual unsigned int GetValueSize() const = 0;

    virtual bool Valid() const = 0;
    virtual void Next() = 0;

    //! Get best block at the time this cursor was created
    const uint256 &GetBestBlock() const { return hashBlock; }
private:
    uint256 hashBlock;
};

/** Abstract view on the open txout dataset. */
class CCoinsView
{
public:
    /** Retrieve the Coin (unspent transaction output) for a given outpoint.
     *  Returns true only when an unspent coin was found, which is returned in coin.
     *  When false is returned, coin's value is unspecified.
     */
    virtual bool GetCoin(const COutPoint &outpoint, Coin &coin) const;

    //! Just check whether a given outpoint is unspent.
    virtual bool HaveCoin(const COutPoint &outpoint) const;

    //! Retrieve the block hash whose state this CCoinsView currently represents
    virtual uint256 GetBestBlock() const;

    //! Retrieve the range of blocks that may have been only partially written.
    //! If the database is in a consistent state, the result is the empty vector.
    //! Otherwise, a two-element vector is returned consisting of the new and
    //! the old block hash, in that order.
    virtual std::vector<uint256> GetHeadBlocks() const;

    //! Do a bulk modification (multiple Coin changes + BestBlock change).
    //! The cursor iterates only over dirty/flagged entries in the child cache.
    virtual bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock);

    //! Get a cursor to iterate over the whole state
    virtual CCoinsViewCursor *Cursor() const;

    //! As we use CCoinsViews polymorphically, have a virtual destructor
    virtual ~CCoinsView() {}

    //! Estimate database size (0 if not implemented)
    virtual size_t EstimateSize() const { return 0; }
};


/** CCoinsView backed by another CCoinsView */
class CCoinsViewBacked : public CCoinsView
{
protected:
    CCoinsView *base;

public:
    CCoinsViewBacked(CCoinsView *viewIn);
    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    std::vector<uint256> GetHeadBlocks() const override;
    void SetBackend(CCoinsView &viewIn);
    bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256 &hashBlock) override;
    CCoinsViewCursor *Cursor() const override;
    size_t EstimateSize() const override;
};


/** CCoinsView that adds a memory cache for transactions to another CCoinsView */
class CCoinsViewCache : public CCoinsViewBacked
{
protected:
    /**
     * Make mutable so that we can "fill the cache" even from Get-methods
     * declared as "const".
     */
    mutable uint256 hashBlock;
    // Note: declaration order is load-bearing. m_cache_coins_memory_resource
    // must precede cacheCoins because cacheCoins's allocator stores a pointer
    // to it, which is dereferenced from cacheCoins's destructor (and from
    // every allocate/deallocate). Reordering these is undefined behavior.
    mutable CCoinsMapMemoryResource m_cache_coins_memory_resource{};
    // cacheCoins, m_sentinel (and the underlying memory resource) must only be
    // accessed while cs_main is held. PoolResource is not thread-safe — concurrent
    // access from multiple threads is undefined behavior.
    /* The starting sentinel of the flagged entry circular doubly linked list. */
    mutable CoinsCachePair m_sentinel;
    mutable CCoinsMap cacheCoins;

    /* Cached dynamic memory usage for the inner Coin objects. */
    mutable size_t cachedCoinsUsage;

public:
    CCoinsViewCache(CCoinsView *baseIn);

    /**
     * By deleting the copy constructor, we prevent accidentally using it when one intends to create a cache on top of a base cache.
     */
    CCoinsViewCache(const CCoinsViewCache &) = delete;

    // Standard CCoinsView methods
    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    uint256 GetBestBlock() const override;
    void SetBestBlock(const uint256 &hashBlock);
    bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256 &hashBlock) override;
    CCoinsViewCursor* Cursor() const override {
        throw std::logic_error("CCoinsViewCache cursor iteration not supported.");
    }

    /**
     * Check if we have the given utxo already loaded in this cache.
     * The semantics are the same as HaveCoin(), but no calls to
     * the backing CCoinsView are made.
     */
    bool HaveCoinInCache(const COutPoint &outpoint) const;

    /**
     * Return a reference to Coin in the cache, or a pruned one if not found. This is
     * more efficient than GetCoin.
     *
     * Generally, do not hold the reference returned for more than a short scope.
     * While the current implementation allows for modifications to the contents
     * of the cache while holding the reference, this behavior should not be relied
     * on! To be safe, best to not hold the returned reference through any other
     * calls to this cache.
     */
    const Coin& AccessCoin(const COutPoint &output) const;

    /**
     * Add a coin. Set potential_overwrite to true if a non-pruned version may
     * already exist.
     */
    void AddCoin(const COutPoint& outpoint, Coin&& coin, bool potential_overwrite);

    /**
     * Spend a coin. Pass moveto in order to get the deleted data.
     * If no unspent output exists for the passed outpoint, this call
     * has no effect.
     */
    bool SpendCoin(const COutPoint &outpoint, Coin* moveto = nullptr);

    /**
     * Push the modifications applied to this cache to its base and wipe local state.
     * Failure to call this method or Sync() before destruction will cause the changes
     * to be forgotten.
     * If false is returned, the state of this cache (and its backing view) will be undefined.
     */
    bool Flush();

    /**
     * Destructively reset the contents of this cache to a fresh, empty state.
     * Used after Flush() to return pre-allocated memory to the OS and start
     * the next IBD flush cycle with a clean pool.
     */
    void ReallocateCache();

    /**
     * Push the modifications applied to this cache to its base while retaining
     * the contents of this cache (except for spent coins, which are erased).
     * Failure to call this method or Flush() before destruction will cause the changes
     * to be forgotten.
     * If false is returned, the state of this cache (and its backing view) will be undefined.
     */
    bool Sync();

    /**
     * Removes the UTXO with the given outpoint from the cache, if it is
     * not modified.
     */
    void Uncache(const COutPoint &outpoint);

    //! Calculate the size of the cache (in number of transaction outputs)
    unsigned int GetCacheSize() const;

    //! Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;

    /**
     * Amount of bitcoins coming in to a transaction
     * Note that lightweight clients may not know anything besides the hash of previous transactions,
     * so may not be able to calculate this.
     *
     * @param[in] tx	transaction for which we are checking input total
     * @return	Sum of value of all inputs (scriptSigs)
     */
    CAmount GetValueIn(const CTransaction& tx) const;

    //! Check whether all prevouts of the transaction are present in the UTXO set represented by this view
    bool HaveInputs(const CTransaction& tx) const;

private:
    CCoinsMap::iterator FetchCoin(const COutPoint &outpoint) const;
};

//! Utility function to add all of a transaction's outputs to a cache.
// When check is false, this assumes that overwrites are only possible for coinbase transactions.
// When check is true, the underlying view may be queried to determine whether an addition is
// an overwrite.
// TODO: pass in a boolean to limit these possible overwrites to known
// (pre-BIP34) cases.
void AddCoins(CCoinsViewCache& cache, const CTransaction& tx, int nHeight, bool check = false);

//! Utility function to find any unspent output with a given txid.
// This function can be quite expensive because in the event of a transaction
// which is not found in the cache, it can cause up to MAX_OUTPUTS_PER_BLOCK
// lookups to database, so it should be used with care.
const Coin& AccessByTxid(const CCoinsViewCache& cache, const uint256& txid);

#endif // BITCOIN_COINS_H
