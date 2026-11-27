// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINSTATE_H
#define BITCOIN_CHAINSTATE_H

#include <connecttrace.h>
#include <cs_main.h>

#include <checkqueue.h>
#include <coinsview.h>
#include <coinsviewcache.h>
#include <coinsviews.h>
#include <sync.h>
#include <chain.h>
#include <blockindex.h>
#include <diskblockpos.h>
#include <xfieldhistorymap.h>
#include <undo.h>
#include <policy/packages.h>
#include <scriptcheck.h>
#include <atomic>
#include <cassert>
#include <map>
#include <memory>
#include <set>

struct CBlockLocator;
class CBlockTreeDB;
class CCoinsViewDB;
class ChainstateManager;
class CIssuedColorIds;
class CTxMemPool;
class CCoinsViewMemPool;
struct MempoolAcceptOptions;
struct MempoolAcceptResult;
struct DisconnectedBlockTransactions;


enum class FlushStateMode {
    NONE,
    IF_NEEDED,
    PERIODIC,
    ALWAYS
};

enum DisconnectResult
{
    DISCONNECT_OK,      // All good.
    DISCONNECT_UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    DISCONNECT_FAILED   // Something else went wrong.
};

struct CBlockIndexWorkComparator
{
    bool operator()(const CBlockIndex *pa, const CBlockIndex *pb) const {
        // First sort by most total work, ...
        if (pa->nHeight > pb->nHeight) return false;
        if (pa->nHeight < pb->nHeight) return true;

        // ... then by earliest time received, ...
        if (pa->nSequenceId < pb->nSequenceId) return false;
        if (pa->nSequenceId > pb->nSequenceId) return true;

        // Use pointer address as tie breaker (should only happen with blocks
        // loaded from disk, as those all have id 0).
        if (pa < pb) return false;
        if (pa > pb) return true;

        // Identical blocks.
        return false;
    }
};

/**
 * Chainstate stores and provides an API to update our local knowledge of the
 * current best chain and header tree.
 *
 * It generally provides access to the current block tree, as well as functions
 * to provide new data, which it will appropriately validate and incorporate in
 * its state as necessary.
 *
 * Eventually, the API here is targeted at being exposed externally as a
 * consumable libconsensus library, so any functions added must only call
 * other class member functions, pure functions in other parts of the consensus
 * library, callbacks via the validation interface, or read/write-to-disk
 * functions (eventually this will also be via callbacks).
 */
class Chainstate {
private:
    ChainstateManager& m_chainman;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    std::set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    Mutex cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    int32_t nBlockSequenceId = 1;
    /** Decreasing counter (used by subsequent preciousblock calls). */
    int32_t nBlockReverseSequenceId = -1;
    /** block height for the last block that preciousblock has been applied to. */
    int nLastPreciousHeight = 0;

    /** In order to efficiently track invalidity of headers, we keep the set of
      * blocks which we tried to connect and found to be invalid here (ie which
      * were set to BLOCK_FAILED_VALID since the last restart). We can then
      * walk this set and check if a new header is a descendant of something in
      * this set, preventing us from having to walk mapBlockIndex when we try
      * to connect a bad block and fail.
      *
      * While this is more complicated than marking everything which descends
      * from an invalid block as invalid at the time we discover it to be
      * invalid, doing so would require walking all of mapBlockIndex to find all
      * descendants. Since this case should be very rare, keeping track of all
      * BLOCK_FAILED_VALID blocks in a set should be just fine and work just as
      * well.
      *
      * Because we already walk mapBlockIndex in height-order at startup, we go
      * ahead and mark descendants of invalid blocks as FAILED_CHILD at that time,
      * instead of putting things in this set.
      */
    std::set<CBlockIndex*> m_failed_blocks;

    /**
     * the ChainState CriticalSection
     * A lock that must be held when modifying this ChainState - held in ActivateBestChain()
     */
    Mutex m_cs_chainstate;

    //! The coins views; null until InitCoinsDB().
    std::unique_ptr<CoinsViews> m_coins_views;

    //! When FlushStateToDisk last wrote the block index and flushed the coins, in microseconds.
    int64_t m_last_write GUARDED_BY(cs_main){0};
    int64_t m_last_flush GUARDED_BY(cs_main){0};

    //! Updated as blocks connect and disconnect; NodeContext owns it.
    CTxMemPool& m_mempool;

    //! Set once IsInitialBlockDownload() has returned false; it then stays false.
    mutable std::atomic<bool> m_cached_finished_ibd{false};

public:
    Chainstate(ChainstateManager& chainman, CTxMemPool& mempool);

    ChainstateManager& Chainman() const { return m_chainman; }
    //! The mempool this chainstate updates as blocks connect and disconnect.
    CTxMemPool& GetMempool() const { return m_mempool; }

    /** Whether tx is final in the next block on the active tip, by IsFinalTx with the next
     *  height and, with LOCKTIME_MEDIAN_TIME_PAST, the tip's median time past. */
    bool CheckFinalTx(const CTransaction& tx, int flags = -1) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    //! Whether the LockPoints are still valid on the active chain.
    bool TestLockPointValidity(const LockPoints* lp) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Whether tx is BIP 68 final in the next block on the active tip, with its inputs from
     *  viewMemPool. Stores the height, time and needed block in lp, or evaluates lp as given
     *  with useExistingLockPoints. lp is not valid when this returns false. */
    bool CheckSequenceLocks(const CTransaction& tx, int flags, CCoinsViewMemPool& viewMemPool, LockPoints* lp = nullptr, bool useExistingLockPoints = false) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Expires old mempool transactions, then trims the mempool to its max size, dropping
     *  the coins the removed transactions spent from the coins tip. */
    void LimitMempoolSize() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    //! Checks block is completely valid from start to finish, built on the active tip.
    bool TestBlockValidity(CValidationState& state, const CBlock& block, bool fCheckPOW = true, bool fCheckMerkleRoot = true) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Loads mempool.dat into this chainstate's mempool. */
    bool LoadMempool();
    /** Bytes the mempool uses; FlushStateToDisk lends the rest of -maxmempool to the coins cache. */
    size_t MempoolUsage() const;

    /** (Try to) add a transaction to this chainstate's mempool; result says why not, or what it replaced.
     *  Returns false also when only its inputs are missing, with a valid state. */
    bool AcceptToMemoryPool(const CTransactionRef& tx, const MempoolAcceptOptions& options, MempoolAcceptResult& result) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Puts the transactions not already in this chainstate's mempool into package; the others get a duplicate rejection in results. */
    void FilterMempoolDuplicates(const std::vector<CTransaction>& txns, Package& package, PackageValidationState& results) const;

    /**
     * Submits the package to this chainstate's mempool after validation checks.
     * It gives granular results with the state of each transaction in the package and
     * the state of the package as a whole.
     *
     * Admission is NOT atomic. Transactions are evaluated and admitted individually in
     * package order. A false return value means at least one transaction was rejected, but
     * earlier transactions in the package may already have been admitted. This is intentional:
     * packages are a submission convenience, not an all-or-nothing commit. Callers must
     * inspect per-transaction results to determine which transactions were admitted.
     * Results describe the mempool after the whole package: a transaction a later one
     * trimmed is "mempool full", and one a later one replaced is "replaced-in-package".
     *
     * @param package The package of transactions to be submitted.
     * @param state A reference to the validation state that tracks the package validation results.
     * @param results A reference to the package validation state that records the validation outcome of each transaction.
     * @param options How the mempool accepts each transaction.
     * @return True if all transactions in the package were successfully accepted, false otherwise.
     */
    bool SubmitPackageToMempool(const Package& package, CValidationState& state, PackageValidationState& results, const MempoolAcceptOptions& options);

    CChain m_chain;

    std::unique_ptr< CCheckQueue<CScriptCheck> >scriptcheckqueue;

    /** Opens the coins database; replaces any earlier coins views. */
    void InitCoinsDB(CIssuedColorIds& issuedColorIds, size_t cache_size_bytes, bool in_memory, bool should_wipe);
    /** Creates the coins cache once the database is consistent. */
    void InitCoinsCache() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool CanFlushToDisk() const EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return m_coins_views && m_coins_views->m_cacheview; }
    CCoinsViewCache& CoinsTip() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { assert(m_coins_views && m_coins_views->m_cacheview); return *m_coins_views->m_cacheview; }
    CCoinsViewDB& CoinsDB() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { assert(m_coins_views); return *m_coins_views->m_dbview; }
    void ResetCoinsViews() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { m_coins_views.reset(); }

    /** Loads the block index, then the candidates, best invalid and best header from it. */
    bool LoadBlockIndex() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool ActivateBestChain(CValidationState &state, std::shared_ptr<const CBlock> pblock);

    /**
     * Update the on-disk chain state.
     * The caches and indexes are flushed depending on the mode we're called with
     * if they're too large, if it's been a while since the last write,
     * or always and in all cases if we're in prune mode and are deleting files.
     *
     * If FlushStateMode::NONE is used, then FlushStateToDisk() won't do anything
     * besides checking if we need to prune.
     */
    bool FlushStateToDisk(CValidationState& state, FlushStateMode mode, int nManualPruneHeight = 0);

    /**
     * If a block header hasn't already been seen, call CheckBlockHeader on it, ensure
     * that it doesn't descend from an invalid block, and then add it to mapBlockIndex.
     */
    bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex** ppindex, CXFieldHistoryMap* pxfieldHistory = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool AcceptBlock(const std::shared_ptr<const CBlock>& pblock, CValidationState& state, CBlockIndex** ppindex, bool fRequested, const CDiskBlockPos* dbp, bool* fNewBlock, CXFieldHistoryMap* pxfieldHistory = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    // Block (dis)connection on a given view and issued colorIds:
    DisconnectResult DisconnectBlock(const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view, CIssuedColorIds& issuedColorIds, bool fDryRun = false) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex,
                    CCoinsViewCache& view, CIssuedColorIds& issuedColorIds, bool fJustCheck = false);

    // Block disconnection on our coins tip:
    bool DisconnectTip(CValidationState& state, DisconnectedBlockTransactions *disconnectpool);

    // Manual block validity manipulation:
    bool PreciousBlock(CValidationState& state, CBlockIndex* pindex) LOCKS_EXCLUDED(cs_main);
    bool InvalidateBlock(CValidationState& state, CBlockIndex* pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void ResetBlockFailureFlags(CBlockIndex* pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool ReplayBlocks(CCoinsView* view, CIssuedColorIds& issuedColorIds);
    bool RewindBlockIndex();
    bool LoadGenesisBlock();

    void PruneBlockIndexCandidates();

    void UnloadBlockIndex();

    /** Sets the tip to the coins database's best block; connects the genesis block first on a new database. */
    bool LoadChainTip() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Fills each coin from the mempool, else from the coins tip; one found in neither is unknown or spent, and cleared. */
    void FindCoins(std::map<COutPoint, Coin>& coins);
    /** Whether the node is still catching up: blocks are being loaded, there is no tip,
     *  or the tip is older than -maxtipage. Once false, it stays false. */
    bool IsInitialBlockDownload() const;
    /** Writes the whole state to disk, logging a failure. */
    void ForceFlushStateToDisk();
    /** Prunes block files if due and flushes what that needs, logging a failure. */
    void PruneAndFlush();
    /** The last block of the locator that is on the active chain, or the genesis block. */
    CBlockIndex* FindForkInGlobalIndex(const CBlockLocator& locator) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    /** AcceptToMemoryPool reading inputs through view; a package shares one view across its transactions,
     *  whose inputs need not be on disk yet. */
    bool AcceptSingleTransaction(const CTransactionRef& tx, const MempoolAcceptOptions& options, CCoinsViewMemPool& view, bool in_package, MempoolAcceptResult& result) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool AcceptToMemoryPoolWorker(const CTransactionRef& ptx, const MempoolAcceptOptions& options, CCoinsViewMemPool& mempool_view, bool in_package,
                                  std::vector<COutPoint>& coins_to_uncache, MempoolAcceptResult& result) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** False, with missing_inputs set, if an input is in neither view nor the coins tip. */
    bool DoAllInputsExist(const CTransaction& tx, CValidationState& state, std::vector<COutPoint>& coins_to_uncache, std::vector<COutPoint>& missing_inputs, CCoinsViewCache& view) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Checks tx's scripts against view, first making sure each input view returns is the coins tip's or a mempool transaction's output. */
    bool CheckInputsFromMempoolAndCache(bool in_package, const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view, const CTxMemPool& pool,
                                        unsigned int flags, bool cacheSigStore) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Whether the tip is recent enough for mempool entries to feed the fee estimator. */
    bool IsCurrentForFeeEstimation() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void UpdateTip(const CBlockIndex* pindexNew) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void UpdateMempoolForReorg(DisconnectedBlockTransactions& disconnectpool, bool fAddToMempool) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Evicts the mempool transactions that fail the script checks or colored-coin rules
     *  in force at newBlockHeight, once a soft fork activates there. */
    void RemoveForScriptFlagChange(unsigned int newMempoolScriptFlags, int32_t newBlockHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool ActivateBestChainStep(CValidationState& state, CBlockIndex* pindexMostWork, const std::shared_ptr<const CBlock>& pblock, bool& fInvalidFound, ConnectTrace& connectTrace);
    bool ConnectTip(CValidationState& state, CBlockIndex* pindexNew, const std::shared_ptr<const CBlock>& pblock, ConnectTrace& connectTrace, DisconnectedBlockTransactions &disconnectpool);

    /**
     * Make various assertions about the state of the block index.
     *
     * By default this only executes fully when using the Regtest chain; see: fCheckBlockIndex.
     */
    void CheckBlockIndex();

    void InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void InvalidChainFound(CBlockIndex* pindexNew) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Sets or clears the large-fork and invalid-chain warnings. */
    void CheckForkWarningConditions() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    CBlockIndex* FindMostWorkChain() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void ReceivedBlockTransactions(const CBlock& block, CBlockIndex* pindexNew, const CDiskBlockPos& pos) EXCLUSIVE_LOCKS_REQUIRED(cs_main);


    bool RollforwardBlock(const CBlockIndex* pindex, CCoinsViewCache& inputs, CIssuedColorIds& issuedColorIds) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
} ;

#endif //BITCOIN_CHAINSTATE_H