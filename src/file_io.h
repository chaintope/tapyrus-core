// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FILE_IO_H
#define BITCOIN_FILE_IO_H

constexpr size_t REINDEX_BUFFER_SIZE = 32 * 1000000;  //  use large 32MB buffer to handle any block size
enum class FlushStateMode {
    NONE,
    IF_NEEDED,
    PERIODIC,
    ALWAYS
};

/** Load the mempool from disk. */
bool LoadMempool();

/** Dump the mempool to disk. */
bool DumpMempool();

/** Import blocks from an external file */
bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos *dbp = nullptr, CXFieldHistoryMap* pxfieldHistory = nullptr);

/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 *
 * If FlushStateMode::NONE is used, then FlushStateToDisk(...) won't do anything
 * besides checking if we need to prune.
 */
bool FlushStateToDisk(CValidationState &state, FlushStateMode mode, int nManualPruneHeight=0);

void  FlushBlockFile(bool fFinalize = false);

/** Functions for disk access for blocks */
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, int height);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);
bool ReadRawBlockFromDisk(std::vector<uint8_t>& block, const CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& message_start);
bool ReadRawBlockFromDisk(std::vector<uint8_t>& block, const CBlockIndex* pindex, const CMessageHeader::MessageStartChars& message_start);

FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly);
/** Open an undo file (rev?????.dat) */
FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);

/** Store block on disk. If dbp is non-nullptr, the file is known to already reside on disk */
CDiskBlockPos SaveBlockToDisk(const CBlock& block, int nHeight, const CDiskBlockPos* dbp);

/** Size of header written by WriteBlockToDisk (network magic + block size) */
static constexpr size_t BLOCK_SERIALIZATION_HEADER_SIZE = CMessageHeader::MESSAGE_START_SIZE + sizeof(unsigned int);

#endif // BITCOIN_FILE_IO_H