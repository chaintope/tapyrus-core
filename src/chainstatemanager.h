// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CHAINSTATEMANAGER_H
#define TAPYRUS_CHAINSTATEMANAGER_H

#include <blockfilemanager.h>
#include <blockindexmap.h>
#include <consensussettings.h>
#include <cs_main.h>
#include <heightderivedconsensusrules.h>
#include <issuedcolorids.h>
#include <sync.h>
#include <uint256.h>
#include <validationcache.h>
#include <verificationprogress.h>
#include <xfieldhistory.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class ArgsManager;
class CBlock;
class CBlockHeader;
class CBlockIndex;
class CChain;
class CCoinsViewCache;
class Chainstate;
class ChainNotifications;
class CTxMemPool;
class CValidationState;

static const int64_t DEFAULT_MAX_TIP_AGE = 24 * 60 * 60;
static const bool DEFAULT_CHECKPOINTS_ENABLED = true;
/** Maximum number of script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 16;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;

/**
 * Owns the node's chain-wide state, as Bitcoin Core's ChainstateManager does.
 * So far it holds the xfield history and the issued colorIds of the active chain,
 * the consensus settings, the block index, the block files, the start-up options,
 * the best block, the best header, the best invalid block, the active chainstate,
 * the verification progress estimate, the validation caches and the
 * notifications it reports to.
 */
class ChainstateManager
{
public:
    /** Read once from tapyrus.conf and the command line at start-up and fixed for the node's lifetime. */
    struct Options {
        /**
         * Reads the chainstate arguments. defaultConsistencyChecks is the chain's
         * default for -checkblockindex; coinCacheUsage is what -dbcache leaves for
         * the in-memory coins cache.
         */
        Options(const ArgsManager& args, bool defaultConsistencyChecks, size_t coinCacheUsage);

        /** -reindex-chainstate: the coins database is rebuilt from the block files. */
        const bool fReindexChainState;
        /** -reloadxfield: the block tree is wiped and the xfield history rebuilt from the block files. */
        const bool fReloadXField;
        /** -checkblockindex: the block index is checked for consistency after every change. */
        const bool fCheckBlockIndex;
        /** -checkpoints: headers that fork from the chain below the last checkpoint are rejected. */
        const bool fCheckpointsEnabled;
        /** Block hash whose ancestors we will assume to have valid scripts without checking them. */
        const uint256 hashAssumeValid;
        /** If the tip is older than this (in seconds), the node is considered to be in initial block download. */
        const int64_t nMaxTipAge;
        /** Script-check worker threads; 0 checks scripts on the connecting thread. */
        const int nScriptCheckThreads;
        /** Bytes the in-memory coins cache may use. */
        const size_t nCoinCacheUsage;

    private:
        static int ParseScriptCheckThreads(const ArgsManager& args);
    };

    //! Declared here and defined in the .cpp, so this header needs only a
    //! forward declaration of Chainstate.
    ChainstateManager();
    ~ChainstateManager();

    /** Sets the options once at start-up; tests set them again for each fixture. */
    void InitOptions(const Options& options);

    const Options& GetOptions() const { assert(m_options); return *m_options; }

    /** Adds the network's genesis block's xfield entries; safe to call again (entries already present are kept). */
    void InitXFieldHistory();

    CXFieldHistory& XFieldHistory() { return m_xfield_history; }

    /** Sets the consensus settings once at start-up; tests set them again for each fixture. */
    void InitConsensusSettings(std::unique_ptr<const ConsensusSettings> settings);

    const ConsensusSettings& Settings() const;

    /** The rules of this block, from the active chain's history or a working copy that may run ahead of the tip. */
    HeightDerivedConsensusRules RulesAt(const CBlockIndex& block) const;
    HeightDerivedConsensusRules RulesAt(const CBlockIndex& block, const CXFieldHistoryMap& history) const;
    /** The rules of the next block on prev. */
    HeightDerivedConsensusRules RulesAfter(const CBlockIndex& prev) const;
    HeightDerivedConsensusRules RulesAfter(const CBlockIndex& prev, const CXFieldHistoryMap& history) const;
    /**
     * The rules of the history's latest entries, for a block whose position is not
     * known. Every registered soft fork counts as active.
     */
    HeightDerivedConsensusRules RulesLatest() const;
    HeightDerivedConsensusRules RulesLatest(const CXFieldHistoryMap& history) const;

    /** The NON_REISSUABLE and NFT colorIds issued on the active chain; they change with the tip. */
    CIssuedColorIds& IssuedColorIds() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return m_issued_colorids; }

    /** Every block header we know of, by hash, and the block tree database. */
    BlockIndexMap& BlockIndex() { return m_block_index; }

    /** The best header we've seen so far; getheaders requests start from it. */
    CBlockIndex*& BestHeader() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return m_best_header; }
    /** The highest block found invalid. */
    CBlockIndex*& BestInvalid() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return m_best_invalid; }

    /** The signature and script execution caches; init sizes them with ValidationCache::Resize. */
    ValidationCache& ValidationCaches() { return m_validation_cache; }

    /** How far chain sync has got; readable without cs_main. */
    VerificationProgress& Progress() { return m_progress; }
    //! The estimated progress of the active tip.
    double GuessTipProgress();
    //! A locator for the best header, built on the active chain.
    CBlockLocator BestHeaderLocator() const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Where fatal errors and the fork warnings are reported; NodeContext owns it.
     *  Set at start-up, before anything can fail. */
    void SetNotifications(ChainNotifications* notifications) { m_notifications = notifications; }
    ChainNotifications& Notifications() const { assert(m_notifications); return *m_notifications; }

    /** Creates the block file manager once -prune is known; replaces any earlier one, keeping the block index.
     *  Requires SetNotifications first; the block file manager reports its fatal file errors through them. */
    void InitBlockFileManager(const BlockFileManager::Options& options);

    BlockFileManager& GetBlockFileManager() { assert(m_block_file_manager); return *m_block_file_manager; }
    /** Destroys the block file manager before the notifications it reports to. */
    void ReleaseBlockFileManager() { m_block_file_manager.reset(); }

    /** Set while the block files are reindexed; restored from the block tree when an earlier reindex was interrupted. */
    bool IsReindexing() const { return m_reindexing; }
    void SetReindexing(bool reindexing) { m_reindexing = reindexing; }
    /** True if any block files have ever been pruned. */
    bool HavePruned() const EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return m_have_pruned; }
    /** Records in the block tree, once, that block files have been pruned. */
    void RecordPruned() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Reads the pruned and reindexing flags from the block tree. */
    void LoadPersistentFlags() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Forgets the pruned flag, as when the block index is unloaded. */
    void UnloadPersistentFlags() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { m_have_pruned = false; }

    /** True while blocks are imported or reindexed; incoming blocks are ignored then. */
    bool LoadingBlocks();
    /** True for -reindex or -reloadxfield: the block tree and the coins database are wiped and rebuilt from the block files. */
    bool ShouldWipeBlockTree();
    /** Reads the block of this index entry, checking its header against the rules at its height. */
    bool ReadBlock(CBlock& block, const CBlockIndex* pindex);
    /** Whether the block of this index entry has been pruned. */
    bool IsBlockPruned(const CBlockIndex* pblockindex) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Creates the chainstate, which updates mempool as the tip moves; replaces any earlier one. */
    void InitializeChainstate(CTxMemPool& mempool);
    bool HasChainstate() const { return m_active_chainstate != nullptr; }

    Chainstate& ActiveChainstate() const;
    CChain& ActiveChain() const;

    /** Import blocks from an external file */
    bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos* dbp = nullptr, CXFieldHistoryMap* pxfieldHistory = nullptr);

    /**
     * Checks a block, stores it and activates the best chain; the block need not
     * extend the tip. fForceProcessing stores it even if it was not requested;
     * fNewBlock, if given, tells whether it was new. Returns false only if the
     * block could not be stored or activating the best chain failed.
     */
    bool ProcessNewBlock(const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool* fNewBlock) LOCKS_EXCLUDED(cs_main);
    /**
     * Adds the headers to the block index, checking each against its parent; the
     * headers need not extend the tip. ppindex gets the last one accepted and
     * first_invalid the first one rejected.
     */
    bool ProcessNewBlockHeaders(const std::vector<CBlockHeader>& headers, CValidationState& state, const CBlockIndex** ppindex = nullptr, CBlockHeader* first_invalid = nullptr) LOCKS_EXCLUDED(cs_main);

    /** Loads the block index and the block file info from the block tree, unless the node reindexes. */
    bool LoadBlockIndex() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Forgets the block index, the active tip and mempool's transactions, so the index can be loaded again.
     *  Not for use once peers are connected, which assume a consistent index. */
    void UnloadBlockIndex();
    /** Rewinds the active chain to the blocks it has data for, then flushes. */
    bool RewindBlockIndex();

    //! Starts the script-check queue with the -par threads from the options.
    void StartScriptCheckWorkerThreads();
    /** The last checkpoint in the block index, or null when -checkpoints is off. */
    const CBlockIndex* GetLastCheckpointIfEnabled() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! The hash of the active tip. getblocktemplate long-polls on it; the tip
    //! update sets it and RPC stop wakes the waiters.
    Mutex m_best_block_mutex;
    CConditionVariable m_best_block_cv;
    uint256 m_best_block GUARDED_BY(m_best_block_mutex);

private:
    bool LoadBlockIndexDB() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Tells the GUI when the best header changes. */
    void NotifyHeaderTip() LOCKS_EXCLUDED(cs_main);

    HeightDerivedConsensusRules BuildRules(std::optional<int32_t> height, const CXFieldHistoryMap& history) const;

    std::unique_ptr<const Options> m_options;
    CXFieldHistory m_xfield_history;
    CIssuedColorIds m_issued_colorids GUARDED_BY(cs_main);
    CBlockIndex* m_best_header GUARDED_BY(cs_main){nullptr};
    CBlockIndex* m_best_invalid GUARDED_BY(cs_main){nullptr};
    VerificationProgress m_progress;
    ValidationCache m_validation_cache;
    std::unique_ptr<const ConsensusSettings> m_consensus_settings;
    //! Declared before the block file manager, which keeps a reference to it.
    BlockIndexMap m_block_index;
    ChainNotifications* m_notifications{nullptr};
    //! Built from the start-up options, so it cannot be a plain member.
    std::unique_ptr<BlockFileManager> m_block_file_manager;
    //! Read by the network thread while the import thread clears it.
    std::atomic_bool m_reindexing{false};
    bool m_have_pruned GUARDED_BY(cs_main) = false;
    //! The only chainstate; the manager decides which chainstate is active, as in Core.
    std::unique_ptr<Chainstate> m_active_chainstate;
    //! Disk positions of reindexed blocks whose parent is not loaded yet, by parent hash.
    //! Only the import thread uses it.
    std::multimap<uint256, CDiskBlockPos> m_blocks_unknown_parent;
    //! The best header NotifyHeaderTip last reported.
    CBlockIndex* m_last_notified_header GUARDED_BY(cs_main){nullptr};
};

#endif // TAPYRUS_CHAINSTATEMANAGER_H
