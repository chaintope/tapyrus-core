// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_BLOCKFILEMANAGER_H
#define TAPYRUS_BLOCKFILEMANAGER_H

#include <cs_main.h>
#include <diskblockpos.h>
#include <fs.h>
#include <protocol.h>
#include <sync.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

class BlockIndexMap;
class CBlock;
class CBlockIndex;
class CBlockUndo;
class CValidationState;
class HeightDerivedConsensusRules;

/**
 * Owns the block and undo files (blk?????.dat and rev?????.dat), as Bitcoin
 * Core's BlockManager does: where the next block goes, reading blocks back,
 * and pruning. The file bookkeeping is guarded by cs_LastBlockFile only.
 * It records where each block is in the block index, BlockIndexMap, which
 * ChainstateManager owns and passes in.
 */
class BlockFileManager
{
public:
    /** Fixed for the node's lifetime, read from -prune at start-up. */
    struct Options {
        /** True if we're running in -prune mode. */
        const bool fPruneMode;
        /** Number of bytes of block files that we're trying to stay below. */
        const uint64_t nPruneTarget;
        /** Pruning waits until the chain is at least this long. */
        const uint32_t nPruneAfterHeight;
    };

    /** The maximum size of a blk?????.dat file (since 0.8) */
    static constexpr uint32_t MAX_BLOCKFILE_SIZE{0x8000000}; // 128 MiB
    /** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
    static constexpr uint32_t BLOCKFILE_CHUNK_SIZE{0x1000000}; // 16 MiB
    /** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
    static constexpr uint32_t UNDOFILE_CHUNK_SIZE{0x100000}; // 1 MiB
    /** Size of header written before serialized block/undo data on disk (4 magic bytes + 4 length bytes) */
    static constexpr uint32_t STORAGE_HEADER_BYTES{CMessageHeader::MESSAGE_START_SIZE + sizeof(unsigned int)};
    /** Minimum disk space required - used in CheckDiskSpace() */
    static constexpr uint64_t nMinDiskSpace{52428800};
    /** Block files containing a block-height within MIN_BLOCKS_TO_KEEP of chainActive.Tip() will not be pruned. */
    static constexpr uint32_t MIN_BLOCKS_TO_KEEP{288};
    // Require that user allocate at least 550MB for block & undo files (blk???.dat and rev???.dat)
    // At 1MB per block, 288 blocks = 288MB.
    // Add 15% for Undo data = 331MB
    // Add 20% for Orphan block rate = 397MB
    // We want the low water mark after pruning to be at least 397 MB and since we prune in
    // full block file chunks, we need the high water mark which triggers the prune to be
    // one 128MB block file + added 15% undo data = 147MB greater for a total of 545MB
    // Setting the target to > than 550MB will make it likely we can respect the target.
    static constexpr uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES{550 * 1024 * 1024};

    BlockFileManager(const Options& options, BlockIndexMap& blockIndex);

    const Options& GetOptions() const { return m_options; }

    /** Guards the file bookkeeping below; FlushStateToDisk holds it across a whole flush. */
    RecursiveMutex cs_LastBlockFile;

    /** Set while blocks are imported from files at start-up. */
    std::atomic_bool fImporting{false};
    /** Set while the block files are reindexed. */
    std::atomic_bool fReindex{false};
    /** True if any block files have ever been pruned. */
    bool fHavePruned{false};

    /** True while blocks are imported or reindexed; incoming blocks are ignored then. */
    bool LoadingBlocks() const { return fImporting || fReindex; }

    /** Reads the file info and the pruned and reindexing flags from the block tree, and checks the blk files the index needs exist. */
    bool LoadBlockFileInfo() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Forgets the file info, as when the block index is unloaded. */
    void Unload();

    /** Store block on disk. If dbp is non-nullptr, the file is known to already reside on disk */
    CDiskBlockPos SaveBlockToDisk(const CBlock& block, int nHeight, const CDiskBlockPos* dbp);
    /** Writes the undo data of a connected block, once, and records where in the index. */
    bool WriteUndoDataForBlock(const CBlockUndo& blockundo, CValidationState& state, CBlockIndex* pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void FlushBlockFile(bool fFinalize = false);

    /** Functions for disk access for blocks */
    bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const HeightDerivedConsensusRules& consensusAtHeight) const;
    /** consensusAtHeight are the rules at pindex's height, which the header is checked against. */
    bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const HeightDerivedConsensusRules& consensusAtHeight) const;
    bool ReadRawBlockFromDisk(std::vector<uint8_t>& block, const CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& message_start) const;
    bool ReadRawBlockFromDisk(std::vector<uint8_t>& block, const CBlockIndex* pindex, const CMessageHeader::MessageStartChars& message_start) const;
    bool UndoReadFromDisk(CBlockUndo& blockundo, const CBlockIndex* pindex, const CBlock& block) const;

    /** Open a block file (blk?????.dat) */
    FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly = false) const;
    /** Open an undo file (rev?????.dat) */
    FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly = false) const;
    /** Translation to a filesystem path */
    fs::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix) const;

    /** Check whether enough disk space is available for an incoming block */
    bool CheckDiskSpace(uint64_t nAdditionalBytes = 0, bool blocks_dir = false) const;

    /** Get block file info entry for one block file */
    CBlockFileInfo* GetBlockFileInfo(size_t n);

    /** Calculate the amount of disk space the block & undo files currently use */
    uint64_t CalculateCurrentUsage();

    /**
     *  Mark one block file as pruned.
     */
    void PruneOneBlockFile(const int fileNumber) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     *  Actually unlink the specified files
     */
    void UnlinkPrunedFiles(const std::set<int>& setFilesToPrune) const;

    /** Makes the next flush check whether files can be pruned. */
    void SetCheckForPruning();

    /**
     * The files a flush can prune, marked pruned in the index: up to
     * nManualPruneHeight when it is positive, else those over the prune target
     * if a check is due. Records in the block tree that files have been pruned.
     * tip is the active chain's tip; fInitialBlockDownload leaves a larger buffer.
     */
    void FindFilesToPruneOnFlush(std::set<int>& setFilesToPrune, int nManualPruneHeight, const CBlockIndex* tip, bool fInitialBlockDownload) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Writes the changed file info and block index entries to the block tree. */
    bool WriteBlockIndexDB() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * If we're using -prune with -reindex, then delete block files that will be ignored by the
     * reindex.  Since reindexing works by starting at block file 0 and looping until a blockfile
     * is missing, do the same here to delete any later block files after a gap.  Also delete all
     * rev files since they'll be rewritten by the reindex anyway.  This ensures that vinfoBlockFile
     * is in sync with what's actually on disk by the time we start downloading, so that pruning
     * works correctly.
     */
    void CleanupBlockRevFiles() const;

private:
    bool FindBlockPos(CDiskBlockPos& pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false);
    bool FindUndoPos(CValidationState& state, int nFile, CDiskBlockPos& pos, unsigned int nAddSize);
    bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart) const;
    bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart) const;
    FILE* OpenDiskFile(const CDiskBlockPos& pos, const char* prefix, bool fReadOnly) const;

    /* Calculate the block/rev files to delete based on height specified by user with RPC command pruneblockchain */
    void FindFilesToPruneManual(std::set<int>& setFilesToPrune, int nManualPruneHeight, const CBlockIndex* tip) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Prune block and undo files (blk???.dat and undo???.dat) so that the disk space used is less than a user-defined target.
     * The user sets the target (in MB) on the command line or in config file.  This will be run on startup and whenever new
     * space is allocated in a block or undo file, staying below the target. Changing back to unpruned requires a reindex
     * (which in this case means the blockchain must be re-downloaded.)
     *
     * Pruning functions are called from FlushStateToDisk when fCheckForPruning has been set.
     * Block and undo files are deleted in lock-step (when blk00003.dat is deleted, so is rev00003.dat.)
     * Pruning cannot take place until the longest chain is at least a certain length (100000 on mainnet, 1000 on testnet, 1000 on dev).
     * Pruning will never delete a block within a defined distance (currently 288) from the active chain's tip.
     * The block index is updated by unsetting HAVE_DATA and HAVE_UNDO for any blocks that were stored in the deleted files.
     * A db flag records the fact that at least some block files have been pruned.
     *
     * @param[out]   setFilesToPrune   The set of file indices that can be unlinked will be returned
     */
    void FindFilesToPrune(std::set<int>& setFilesToPrune, const CBlockIndex* tip, bool fInitialBlockDownload) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    const Options m_options;
    BlockIndexMap& m_block_index;

    std::vector<CBlockFileInfo> vinfoBlockFile GUARDED_BY(cs_LastBlockFile);
    int nLastBlockFile GUARDED_BY(cs_LastBlockFile) = 0;
    /** Dirty block file entries. */
    std::set<int> setDirtyFileInfo GUARDED_BY(cs_LastBlockFile);
    /** Set to check at the next flush whether there are block/undo files
     *  that should be deleted.  Set on startup or if we allocate more file
     *  space when we're in prune mode.
     */
    bool fCheckForPruning GUARDED_BY(cs_LastBlockFile) = false;
};

#endif // TAPYRUS_BLOCKFILEMANAGER_H
