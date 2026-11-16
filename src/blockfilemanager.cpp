// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockfilemanager.h>

#include <abortnode.h>
#include <checkblock.h>
#include <clientversion.h>
#include <federationparams.h>
#include <hash.h>
#include <heightderivedconsensusrules.h>
#include <streams.h>
#include <txdb.h>
#include <undo.h>
#include <util.h>
#include <utilstrencodings.h>
#include <validation.h>

#include <charconv>
#include <map>
#include <string>

BlockFileManager::BlockFileManager(const Options& options)
    : m_options(options)
{
}

bool BlockFileManager::LoadBlockFileInfo(CBlockTreeDB& blocktree)
{
    LOCK(cs_LastBlockFile);

    // Load block file info
    blocktree.ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        blocktree.ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (blocktree.ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    std::set<int> setBlkDataFiles;
    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
    }

    // Check whether we have ever pruned block & undo files
    blocktree.ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    blocktree.ReadReindexing(fReindexing);
    if(fReindexing) fReindex = true;

    return true;
}

void BlockFileManager::Unload()
{
    LOCK(cs_LastBlockFile);
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    setDirtyFileInfo.clear();
    fHavePruned = false;
}

bool BlockFileManager::FindBlockPos(CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
            nFile++;
            if (vinfoBlockFile.size() <= nFile) {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if ((int)nFile != nLastBlockFile) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nLastBlockFile, vinfoBlockFile[nLastBlockFile].ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown) {
        uint64_t nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        uint64_t nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (m_options.fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos, true)) {
                FILE *file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

bool BlockFileManager::FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    uint64_t nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    uint64_t nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    uint64_t nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (m_options.fPruneMode)
            fCheckForPruning = true;
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos, true)) {
            FILE *file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
            return state.Error("out of disk space");
    }

    return true;
}

bool BlockFileManager::WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart) const
{
    // Open history file to append
    CAutoFile file(OpenBlockFile(pos, /*fReadOnly=*/false), SER_DISK, CLIENT_VERSION);
    if (file.IsNull())
        return error("OpenBlockFile failed for %s while writing block", pos.ToString());

    // Write index header (unbuffered: header must be on disk before we record pos.nPos)
    unsigned int nSize = GetSerializeSize(file, block);
    file << messageStart << nSize;
    pos.nPos += STORAGE_HEADER_BYTES;

    {
        BufferedWriter<CAutoFile> fileout(file);
        fileout << block;
        fileout.flush();
    }

    return true;
}

CDiskBlockPos BlockFileManager::SaveBlockToDisk(const CBlock& block, int nHeight, const CDiskBlockPos* dbp) {
    unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
    CDiskBlockPos blockPos;
    const auto position_known {dbp != nullptr};
    if (position_known) {
        blockPos = *dbp;
    } else {
        // when known, blockPos.nPos points at the offset of the block data in the blk file. that already accounts for
        // the serialization header present in the file (the 4 magic message start bytes + the 4 length bytes = 8 bytes = STORAGE_HEADER_BYTES).
        // we add STORAGE_HEADER_BYTES only for new blocks since they will have the serialization header added when written to disk.
        nBlockSize += static_cast<unsigned int>(STORAGE_HEADER_BYTES);
    }
    if (!FindBlockPos(blockPos, nBlockSize, nHeight, block.GetBlockTime(), position_known)) {
        error("FindBlockPos failed for %s while saving block", blockPos.ToString());
        return CDiskBlockPos();
    }
    if (!position_known) {
        if (!WriteBlockToDisk(block, blockPos, FederationParams().MessageStart())) {
            AbortNode("Failed to write block");
            return CDiskBlockPos();
        }
    }
    return blockPos;
}

bool BlockFileManager::UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart) const
{
    // Open history file to append
    CAutoFile file(OpenUndoFile(pos, /*fReadOnly=*/false), SER_DISK, CLIENT_VERSION);
    if (file.IsNull())
        return error("OpenUndoFile failed for %s while writing block undo", pos.ToString());

    // Write index header (unbuffered: header must be on disk before we record pos.nPos)
    unsigned int nSize = GetSerializeSize(file, blockundo);
    file << messageStart << nSize;
    pos.nPos += STORAGE_HEADER_BYTES;

    {
        BufferedWriter<CAutoFile> fileout(file);
        // Calculate checksum and write undo data + checksum
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        hasher << hashBlock << blockundo;
        fileout << blockundo << hasher.GetHash();
        fileout.flush();
    }

    return true;
}

bool BlockFileManager::WriteUndoDataForBlock(const CBlockUndo& blockundo, CValidationState& state, CBlockIndex* pindex)
{
    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull()) {
        CDiskBlockPos _pos;
        if (!FindUndoPos(state, pindex->nFile, _pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
            return error("ConnectBlock(): FindUndoPos failed");
        if (!UndoWriteToDisk(blockundo, _pos, pindex->pprev->GetBlockHash(), FederationParams().MessageStart()))
            return AbortNode(state, "Failed to write undo data");

        // update nUndoPos in block index
        pindex->nUndoPos = _pos.nPos;
        pindex->nStatus |= BLOCK_HAVE_UNDO;
        setDirtyBlockIndex.insert(pindex);
    }

    return true;
}

void BlockFileManager::FlushBlockFile(bool fFinalize)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);
    bool status = true;

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            status &= TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        status &= FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            status &= TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        status &= FileCommit(fileOld);
        fclose(fileOld);
    }

    if (!status) {
        AbortNode("Flushing block file to disk failed. This is likely the result of an I/O error.");
    }
}

bool BlockFileManager::ReadRawBlockFromDisk(std::vector<uint8_t>& block, const CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& message_start) const
{
    if (pos.nPos < STORAGE_HEADER_BYTES) {
        // If nPos is less than STORAGE_HEADER_BYTES, we can't read the header that precedes the block data.
        // This would cause an unsigned integer underflow when trying to position the file cursor.
        return error("Failed for %s while reading raw block storage header", pos.ToString());
    }
    CAutoFile filein(OpenBlockFile(CDiskBlockPos(pos.nFile, pos.nPos - STORAGE_HEADER_BYTES), /*fReadOnly=*/true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        return error("OpenBlockFile failed for %s while reading raw block", pos.ToString());
    }

    try {
        CMessageHeader::MessageStartChars blk_start;
        unsigned int blk_size;

        filein >> blk_start >> blk_size;

        if (memcmp(blk_start, message_start, CMessageHeader::MESSAGE_START_SIZE)) {
            return error("Block magic mismatch for %s: %s versus expected %s while reading raw block",
                pos.ToString(),
                HexStr(blk_start, blk_start + CMessageHeader::MESSAGE_START_SIZE),
                HexStr(message_start, message_start + CMessageHeader::MESSAGE_START_SIZE));
        }

        if (blk_size > MAX_SIZE) {
            return error("Block data is larger than maximum deserialization size for %s: %s versus %s while reading raw block",
                pos.ToString(), blk_size, MAX_SIZE);
        }

        block.resize(blk_size); // Zeroing of memory is intentional here
        filein.read((char*)block.data(), blk_size);
    } catch(const std::exception& e) {
        return error("Read from block file failed: %s for %s while reading raw block", e.what(), pos.ToString());
    }

    return true;
}

bool BlockFileManager::ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const HeightDerivedConsensusRules& consensusAtHeight) const
{
    block.SetNull();

    // Read raw block bytes (handles header validation internally)
    std::vector<uint8_t> block_data;
    if (!ReadRawBlockFromDisk(block_data, pos, FederationParams().MessageStart())) {
        return false;
    }

    // Deserialize from memory buffer
    try {
        SpanReader spanreader(SER_DISK, CLIENT_VERSION, block_data);
        spanreader >> block;
    } catch (const std::exception& e) {
        return error("Deserialize or I/O error - %s at %s while reading block", e.what(), pos.ToString());
    }

    // The rules come from the trusted block index, never from block.GetHeight()
    // (coinbase prevout.n), which is attacker-controlled.
    CValidationState state;
    if(!CheckBlockHeader(block.GetBlockHeader(), state, consensusAtHeight, true))
        return error("Errors in block header at %s while reading block: %s", pos.ToString(), FormatStateMessage(state));

    return true;
}

bool BlockFileManager::ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const HeightDerivedConsensusRules& consensusAtHeight) const
{
    CDiskBlockPos blockPos;
    {
        LOCK(cs_main);
        blockPos = pindex->GetBlockPos();
    }

    if (!ReadBlockFromDisk(block, blockPos, consensusAtHeight))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
        return error("GetHash() doesn't match index for %s at %s while reading block",
                pindex->ToString(), pindex->GetBlockPos().ToString());
    return true;
}

bool BlockFileManager::ReadRawBlockFromDisk(std::vector<uint8_t>& block, const CBlockIndex* pindex, const CMessageHeader::MessageStartChars& message_start) const
{
    CDiskBlockPos block_pos;
    {
        LOCK(cs_main);
        block_pos = pindex->GetBlockPos();
    }

    return ReadRawBlockFromDisk(block, block_pos, message_start);
}

bool BlockFileManager::UndoReadFromDisk(CBlockUndo& blockundo, const CBlockIndex *pindex, const CBlock& block) const
{
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull()) {
        return error("no undo data available for %s", pos.ToString());
    }

    // Open history file to read
    CAutoFile file(OpenUndoFile(pos, /*fReadOnly=*/true), SER_DISK, CLIENT_VERSION);
    if (file.IsNull())
        return error("OpenUndoFile failed for %s while reading block undo", pos.ToString());
    BufferedReader<CAutoFile> filein(file);

    try {
        // Read block
        CHashVerifier<BufferedReader<CAutoFile>> verifier(&filein); // Use CHashVerifier as reserializing may lose data, c.f. commit d342424301013ec47dc146a4beb49d5c9319d80a
        verifier << pindex->pprev->GetBlockHash();
        verifier >> BlockUndoDeserializer(&blockundo, block);

        uint256 hashChecksum;
        filein >> hashChecksum;

        // Verify checksum
        if (hashChecksum != verifier.GetHash())
            return error("Checksum mismatch at %s while reading block undo", pos.ToString());
    } catch (const std::exception& e) {
        return error("Deserialize or I/O error - %s at %s while reading block undo", e.what(), pos.ToString());
    }

    return true;
}

FILE* BlockFileManager::OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly) const
{
    if (pos.IsNull())
        return nullptr;
    fs::path path = GetBlockPosFilename(pos, prefix);
    fs::create_directories(path.parent_path());
    FILE* file = fsbridge::fopen(path, fReadOnly ? "rb": "rb+");
    if (!file && !fReadOnly)
        file = fsbridge::fopen(path, "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return nullptr;
    }
    if (pos.nPos) {
        // fseek takes a 32-bit long on Windows; undo files can pass 2 GiB.
#ifdef WIN32
        if (_fseeki64(file, pos.nPos, SEEK_SET)) {
#else
        if (fseek(file, pos.nPos, SEEK_SET)) {
#endif
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return nullptr;
        }
    }
    return file;
}

FILE* BlockFileManager::OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) const {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* BlockFileManager::OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) const {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

fs::path BlockFileManager::GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix) const
{
    return GetBlocksDir() / strprintf("%s%05u.dat", prefix, pos.nFile);
}

bool BlockFileManager::CheckDiskSpace(uint64_t nAdditionalBytes, bool blocks_dir) const
{
    uint64_t nFreeBytesAvailable = fs::space(blocks_dir ? GetBlocksDir() : GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

CBlockFileInfo* BlockFileManager::GetBlockFileInfo(size_t n)
{
    LOCK(cs_LastBlockFile);

    return &vinfoBlockFile.at(n);
}

uint64_t BlockFileManager::CalculateCurrentUsage()
{
    LOCK(cs_LastBlockFile);

    uint64_t retval = 0;
    for (const CBlockFileInfo &file : vinfoBlockFile) {
        retval += file.nSize + file.nUndoSize;
    }
    return retval;
}

void BlockFileManager::PruneOneBlockFile(const int fileNumber)
{
    LOCK(cs_LastBlockFile);

    for (const auto& entry : mapBlockIndex) {
        CBlockIndex* pindex = entry.second;
        if (pindex->nFile == fileNumber) {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second) {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator _it = range.first;
                range.first++;
                if (_it->second == pindex) {
                    mapBlocksUnlinked.erase(_it);
                }
            }
        }
    }

    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}

void BlockFileManager::UnlinkPrunedFiles(const std::set<int>& setFilesToPrune) const
{
    for (const auto& fileNum : setFilesToPrune) {
        CDiskBlockPos pos(fileNum, 0);
        fs::remove(GetBlockPosFilename(pos, "blk"));
        fs::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, pos.nPos);
    }
}

void BlockFileManager::FindFilesToPruneManual(std::set<int>& setFilesToPrune, int nManualPruneHeight)
{
    assert(m_options.fPruneMode && nManualPruneHeight > 0);

    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == nullptr)
        return;

    // last block to prune is the lesser of (user-specified height, MIN_BLOCKS_TO_KEEP from the tip)
    unsigned int nLastBlockWeCanPrune = std::min(static_cast<unsigned int>(nManualPruneHeight), static_cast<unsigned int>(chainActive.Tip()->nHeight) - MIN_BLOCKS_TO_KEEP);
    int count=0;
    for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
        if (vinfoBlockFile[fileNumber].nSize == 0 || vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
            continue;
        PruneOneBlockFile(fileNumber);
        setFilesToPrune.insert(fileNumber);
        count++;
    }
    LogPrintf("Prune (Manual): prune_height=%d removed %d blk/rev pairs\n", nLastBlockWeCanPrune, count);
}

void BlockFileManager::FindFilesToPrune(std::set<int>& setFilesToPrune)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == nullptr || m_options.nPruneTarget == 0) {
        return;
    }
    if (static_cast<uint32_t>(chainActive.Tip()->nHeight) <= m_options.nPruneAfterHeight) {
        return;
    }

    unsigned int nLastBlockWeCanPrune = chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count=0;

    if (nCurrentUsage + nBuffer >= m_options.nPruneTarget) {
        // On a prune event, the chainstate DB is flushed.
        // To avoid excessive prune events negating the benefit of high dbcache
        // values, we should not prune too rapidly.
        // So when pruning in IBD, increase the buffer a bit to avoid a re-prune too soon.
        if (IsInitialBlockDownload()) {
            // Since this is only relevant during IBD, we use a fixed 10%
            nBuffer += m_options.nPruneTarget / 10;
        }

        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
                continue;

            if (nCurrentUsage + nBuffer < m_options.nPruneTarget)  // are we below our target?
                break;

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the prod chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
                continue;

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint(BCLog::PRUNE, "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
           m_options.nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
           ((int64_t)m_options.nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
           nLastBlockWeCanPrune, count);
}

void BlockFileManager::SetCheckForPruning()
{
    LOCK(cs_LastBlockFile);
    fCheckForPruning = true;
}

void BlockFileManager::FindFilesToPruneOnFlush(std::set<int>& setFilesToPrune, int nManualPruneHeight, CBlockTreeDB& blocktree)
{
    LOCK(cs_LastBlockFile);
    if (m_options.fPruneMode && (fCheckForPruning || nManualPruneHeight > 0) && !fReindex) {
        if (nManualPruneHeight > 0) {
            FindFilesToPruneManual(setFilesToPrune, nManualPruneHeight);
        } else {
            FindFilesToPrune(setFilesToPrune);
            fCheckForPruning = false;
        }
        if (!setFilesToPrune.empty()) {
            if (!fHavePruned) {
                blocktree.WriteFlag("prunedblockfiles", true);
                fHavePruned = true;
            }
        }
    }
}

bool BlockFileManager::WriteBlockIndexDB(CBlockTreeDB& blocktree)
{
    LOCK(cs_LastBlockFile);
    std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
    vFiles.reserve(setDirtyFileInfo.size());
    for (std::set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
        vFiles.push_back(std::make_pair(*it, &vinfoBlockFile[*it]));
        setDirtyFileInfo.erase(it++);
    }
    std::vector<const CBlockIndex*> vBlocks;
    vBlocks.reserve(setDirtyBlockIndex.size());
    for (std::set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
        vBlocks.push_back(*it);
        setDirtyBlockIndex.erase(it++);
    }
    return blocktree.WriteBatchSync(vFiles, nLastBlockFile, vBlocks);
}

void BlockFileManager::CleanupBlockRevFiles() const
{
    std::map<std::string, fs::path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for -reindex with -prune\n");
    fs::path blocksdir = GetBlocksDir();
    for (fs::directory_iterator it(blocksdir); it != fs::directory_iterator(); it++) {
        if (fs::is_regular_file(*it) &&
            it->path().filename().string().length() == 12 &&
            it->path().filename().string().substr(8,4) == ".dat")
        {
            if (it->path().filename().string().substr(0,3) == "blk")
                mapBlockFiles[it->path().filename().string().substr(3,5)] = it->path();
            else if (it->path().filename().string().substr(0,3) == "rev")
                remove(it->path());
        }
    }

    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by
    // keeping a separate counter.  Once we hit a gap (or if 0 doesn't exist)
    // start removing block files.
    int nContigCounter = 0;
    for (const std::pair<const std::string, fs::path>& item : mapBlockFiles) {
        // Use std::from_chars for locale-independent conversion
        int fileIndex = 0;
        auto result = std::from_chars(item.first.data(), item.first.data() + item.first.size(), fileIndex);
        // Skip files with invalid indices (parsing failed or didn't consume entire string)
        if (result.ec != std::errc{} || result.ptr != item.first.data() + item.first.size()) {
            remove(item.second);
            continue;
        }
        if (fileIndex == nContigCounter) {
            nContigCounter++;
            continue;
        }
        remove(item.second);
    }
}
