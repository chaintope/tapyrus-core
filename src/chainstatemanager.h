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
#include <xfieldhistory.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>

class ArgsManager;
class CBlock;
class CBlockIndex;

/**
 * Owns the node's chain-wide state, as Bitcoin Core's ChainstateManager does.
 * So far it holds the xfield history and the issued colorIds of the active chain,
 * the consensus settings, the block index and the block files.
 */
class ChainstateManager
{
public:
    /** How start-up rebuilds the chainstate: read once from tapyrus.conf and the command line. */
    struct Options {
        explicit Options(const ArgsManager& args);

        /** -reindex-chainstate: the coins database is rebuilt from the block files. */
        const bool fReindexChainState;
        /** -reloadxfield: the block tree is wiped and the xfield history rebuilt from the block files. */
        const bool fReloadXField;
    };

    /** Sets the options once at start-up; tests set them again for each fixture. */
    void InitOptions(const Options& options);

    const Options& GetOptions() const { assert(m_options); return *m_options; }

    /** Adds the genesis block's xfield entries; safe to call again (entries already present are kept). */
    void InitXFieldHistory(const CBlock& genesis);

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

    /** Creates the block file manager once -prune is known; replaces any earlier one, keeping the block index. */
    void InitBlockFileManager(const BlockFileManager::Options& options);

    BlockFileManager& GetBlockFileManager() { assert(m_block_file_manager); return *m_block_file_manager; }

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

private:
    HeightDerivedConsensusRules BuildRules(std::optional<int32_t> height, const CXFieldHistoryMap& history) const;

    std::unique_ptr<const Options> m_options;
    CXFieldHistory m_xfield_history;
    CIssuedColorIds m_issued_colorids GUARDED_BY(cs_main);
    std::unique_ptr<const ConsensusSettings> m_consensus_settings;
    //! Declared before the block file manager, which keeps a reference to it.
    BlockIndexMap m_block_index;
    //! Built from the start-up options, so it cannot be a plain member.
    std::unique_ptr<BlockFileManager> m_block_file_manager;
    //! Read by the network thread while the import thread clears it.
    std::atomic_bool m_reindexing{false};
    bool m_have_pruned GUARDED_BY(cs_main) = false;
};

extern ChainstateManager g_chainstate_manager;

#endif // TAPYRUS_CHAINSTATEMANAGER_H
