// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockfilemanager.h>
#include <consensussettings.h>
#include <chainstatemanager.h>
#include <checkblock.h>
#include <index/txindex.h>
#include <chainnotifications.h>
#include <mempoolaccept.h>
#include <policy/policy.h>
#include <shutdown.h>
#include <trace.h>
#include <validation.h>
#include <xfieldhistory.h>
#include <file_io.h>
#include <deque>
#include <stdexcept>

static const uint64_t MEMPOOL_DUMP_VERSION = 1;

bool Chainstate::LoadMempool()
{
    int64_t nExpiryTimeout = m_mempool.Expiry();
    FILE* filestr = fsbridge::fopen(GetDataDir() / "mempool.dat", "rb");
    CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);
    if (file.IsNull()) {
        LogPrintf("Failed to open mempool file from disk. Continuing anyway.\n");
        return false;
    }

    int64_t count = 0;
    int64_t expired = 0;
    int64_t failed = 0;
    int64_t already_there = 0;
    int64_t nNow = GetTime();

    try {
        uint64_t version;
        file >> version;
        if (version != MEMPOOL_DUMP_VERSION) {
            return false;
        }
        uint64_t num;
        file >> num;
        while (num--) {
            CTransactionRef tx;
            int64_t nTime;
            int64_t nFeeDelta;
            file >> tx;
            file >> nTime;
            file >> nFeeDelta;

            CAmount amountdelta = nFeeDelta;
            if (amountdelta) {
                m_mempool.PrioritiseTransaction(tx->GetHashMalFix(), amountdelta);
            }
            MempoolAcceptResult result;
            if (nTime + nExpiryTimeout > nNow) {
                LOCK(cs_main);
                MempoolAcceptOptions options;
                options.accept_time = nTime;
                AcceptToMemoryPool(tx, options, result);
                if (result.state.IsValid()) {
                    ++count;
                } else {
                    // mempool may contain the transaction already, e.g. from
                    // wallet(s) having loaded it while we were processing
                    // mempool transactions; consider these as valid, instead of
                    // failed, but mark them as 'already there'
                    if (m_mempool.exists(tx->GetHashMalFix())) {
                        ++already_there;
                    } else {
                        ++failed;
                    }
                }
            } else {
                ++expired;
            }
            if (ShutdownRequested())
                return false;
        }
        std::map<uint256, CAmount> mapDeltas;
        file >> mapDeltas;

        for (const auto& i : mapDeltas) {
            m_mempool.PrioritiseTransaction(i.first, i.second);
        }
    } catch (const std::exception& e) {
        LogPrintf("Failed to deserialize mempool data on disk: %s. Continuing anyway.\n", e.what());
        return false;
    }

    LogPrintf("Imported mempool transactions from disk: %i succeeded, %i failed, %i expired, %i already there\n", count, failed, expired, already_there);
    return true;
}

bool DumpMempool(const CTxMemPool& pool)
{
    int64_t start = GetTimeMicros();

    std::map<uint256, CAmount> mapDeltas;
    std::vector<TxMempoolInfo> vinfo;

    {
        LOCK(pool.cs);
        for (const auto &i : pool.mapDeltas) {
            mapDeltas[i.first] = i.second;
        }
        vinfo = pool.infoAll();
    }

    int64_t mid = GetTimeMicros();

    try {
        FILE* filestr = fsbridge::fopen(GetDataDir() / "mempool.dat.new", "wb");
        if (!filestr) {
            return false;
        }

        CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);

        uint64_t version = MEMPOOL_DUMP_VERSION;
        file << version;

        file << (uint64_t)vinfo.size();
        for (const auto& i : vinfo) {
            file << *(i.tx);
            file << (int64_t)i.nTime;
            file << (int64_t)i.nFeeDelta;
            mapDeltas.erase(i.tx->GetHashMalFix());
        }

        file << mapDeltas;
        if (!FileCommit(file.Get()))
            throw std::runtime_error("FileCommit failed");
        file.fclose();
        RenameOver(GetDataDir() / "mempool.dat.new", GetDataDir() / "mempool.dat");
        int64_t last = GetTimeMicros();
        LogPrintf("Dumped mempool: %gs to copy, %gs to dump\n", (mid-start)*MICRO, (last-mid)*MICRO);
    } catch (const std::exception& e) {
        LogPrintf("Failed to dump mempool: %s. Continuing anyway.\n", e.what());
        return false;
    }
    return true;
}


bool ChainstateManager::LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos *dbp, CXFieldHistoryMap* pxfieldHistory)
{
    int64_t nStart = GetTimeMillis();

    uint32_t maxBlockSize = m_xfield_history.GetCurrentMaxBlockSize();
    uint32_t bufferSize;

    if (dbp != nullptr) {
        // Reindexing - use large 32MB buffer to handle any block size
        bufferSize = REINDEX_BUFFER_SIZE;
    } else {
        // Normal operation - use buffer based on current max block size
        bufferSize = maxBlockSize;
    }

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2*bufferSize, bufferSize+8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[CMessageHeader::MESSAGE_START_SIZE];
                blkdat.FindByte(FederationParams().MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> buf;
                if (memcmp(buf, FederationParams().MessageStart(), CMessageHeader::MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                // Size validation
                if (nSize < 80) {
                    continue;
                }
                // During normal operation, validate against maxBlockSize
                // During reindex (dbp != nullptr), skip size validation as it happens in CheckBlock
                if (dbp == nullptr) {
                    // Normal operation - validate against current maxBlockSize
                    if (nSize > maxBlockSize) {
                        continue;
                    }
                } else {
                    // Reindexing - only check against buffer capacity
                    if (nSize > bufferSize) {
                        LogPrint(BCLog::REINDEX, "%s: Skipping block with size %u (exceeds buffer %u)\n", __func__, nSize, bufferSize);
                        continue;
                    }
                    LogPrint(BCLog::REINDEX, "%s: size %u (reindex mode)\n", __func__, nSize);
                }
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
                CBlock& block = *pblock;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                uint256 hash = block.GetHash();
                {
                    LOCK(cs_main);
                    // detect out of order blocks, and store them for later
                    if (hash != Settings().GenesisBlock().GetHash() && !m_block_index.LookupBlockIndex(block.hashPrevBlock)) {
                        LogPrint(BCLog::REINDEX, "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                                block.hashPrevBlock.ToString());
                        if (dbp)
                            m_blocks_unknown_parent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                        continue;
                    }

                    // process in case the block isn't known yet
                    CBlockIndex* pindex = m_block_index.LookupBlockIndex(hash);
                    if (!pindex || (pindex->nStatus & BLOCK_HAVE_DATA) == 0) {
                        CValidationState state;
                        if (ActiveChainstate().AcceptBlock(pblock, state, nullptr, true, dbp, nullptr, pxfieldHistory)) {
                            nLoaded++;
                        }
                        if (state.IsError()) {
                          break;
                      }
                    } else if (hash != Settings().GenesisBlock().GetHash() && pindex->nHeight % 1000 == 0) {
                      LogPrint(BCLog::REINDEX, "%s Block Import: already had block %s at height %d\n", __func__, hash.ToString(), pindex->nHeight);
                    }
                }

                // Activate the genesis block so normal node progress can continue
                if (hash == Settings().GenesisBlock().GetHash()) {
                    CValidationState state;
                    if (!ActiveChainstate().ActivateBestChain(state, nullptr)) {
                        break;
                    }
                }

                NotifyHeaderTip();

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = m_blocks_unknown_parent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        std::shared_ptr<CBlock> pblockrecursive = std::make_shared<CBlock>();
                        const CXFieldHistoryMap& history = pxfieldHistory ? *pxfieldHistory : m_xfield_history;
                        if (m_block_file_manager->ReadBlockFromDisk(*pblockrecursive, it->second, RulesLatest(history)))
                        {
                            LogPrint(BCLog::REINDEX, "%s: Processing out of order child %s of %s\n", __func__, pblockrecursive->GetHash().ToString(),
                                    head.ToString());
                            LOCK(cs_main);
                            CValidationState dummy;
                            if (ActiveChainstate().AcceptBlock(pblockrecursive, dummy, nullptr, true, &it->second, nullptr, pxfieldHistory))
                            {
                                nLoaded++;
                                queue.push_back(pblockrecursive->GetHash());
                            }
                        }
                        range.first++;
                        m_blocks_unknown_parent.erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        Notifications().ReportFatalError(FatalErrorReason::SYSTEM_ERROR, e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

bool Chainstate::FlushStateToDisk(CValidationState& state, FlushStateMode mode, int nManualPruneHeight)
{
    int64_t nMempoolUsage = MempoolUsage();
    LOCK(cs_main);
    std::set<int> setFilesToPrune;
    bool full_flush_completed = false;

    try {
    {
        [[maybe_unused]]const size_t coins_count = CoinsTip().GetCacheSize();
        [[maybe_unused]]const size_t coins_mem_usage = CoinsTip().DynamicMemoryUsage();

        bool fFlushForPrune = false;
        bool fDoFullFlush = false;
        BlockFileManager& blockFileManager = m_chainman.GetBlockFileManager();
        LOCK(blockFileManager.cs_LastBlockFile);
        blockFileManager.FindFilesToPruneOnFlush(setFilesToPrune, nManualPruneHeight, m_chain.Tip(), IsInitialBlockDownload(), m_chainman.IsReindexing());
        if (!setFilesToPrune.empty()) m_chainman.RecordPruned();
        fFlushForPrune = !setFilesToPrune.empty();
        int64_t nNow = GetTimeMicros();
        // Avoid writing/flushing immediately after startup.
        if (m_last_write == 0) {
            m_last_write = nNow;
        }
        if (m_last_flush == 0) {
            m_last_flush = nNow;
        }
        int64_t nMempoolSizeMax = m_mempool.MaxSize();
        int64_t cacheSize = CoinsTip().DynamicMemoryUsage();
        int64_t nTotalSpace = m_chainman.GetOptions().nCoinCacheUsage + std::max<int64_t>(nMempoolSizeMax - nMempoolUsage, 0);
        // The cache is large and we're within 10% and 10 MiB of the limit, but we have time now (not in the middle of a block processing).
        bool fCacheLarge = mode == FlushStateMode::PERIODIC && cacheSize > std::max((9 * nTotalSpace) / 10, nTotalSpace - MAX_BLOCK_COINSDB_USAGE * 1024 * 1024);
        // The cache is over the limit, we have to write now.
        bool fCacheCritical = mode == FlushStateMode::IF_NEEDED && cacheSize > nTotalSpace;
        // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
        bool fPeriodicWrite = mode == FlushStateMode::PERIODIC && nNow > m_last_write + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
        // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
        bool fPeriodicFlush = mode == FlushStateMode::PERIODIC && nNow > m_last_flush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
        // Combine all conditions that result in a full cache flush.
        // fFlushForPrune is excluded from fDoFullFlush so that a prune flush uses
        // Sync() (writes dirty entries, keeps the cache warm) rather than Flush()
        // (wipes the cache entirely). fFlushForPrune is kept in the outer gate below
        // so that UnlinkPrunedFiles() and the block-index write still run on prune.
        fDoFullFlush = (mode == FlushStateMode::ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush;
        // Write blocks and block index to disk.
        if (fDoFullFlush || fFlushForPrune || fPeriodicWrite) {
            // Depend on nMinDiskSpace to ensure we can write block index
            if (!blockFileManager.CheckDiskSpace(0, true))
                return state.Error("out of disk space");
            // First make sure all block and undo data is flushed to disk.
            blockFileManager.FlushBlockFile();
            // Then update all block file information (which may refer to block and undo files).
            if (!blockFileManager.WriteBlockIndexDB()) {
                m_chainman.Notifications().ReportFatalError(FatalErrorReason::BLOCK_INDEX_WRITE_FAILED, "");
                return state.Error("Failed to write to block index database");
            }
            // Finally remove any pruned files
            if (fFlushForPrune)
                blockFileManager.UnlinkPrunedFiles(setFilesToPrune);
            m_last_write = nNow;
        }
        // Flush best chain related state. This can only be done if the blocks / block index write was also done.
        if ((fDoFullFlush || fFlushForPrune) && !CoinsTip().GetBestBlock().IsNull()) {
            // Typical Coin structures on disk are around 48 bytes in size.
            // Pushing a new one to the database can cause it to be written
            // twice (once in the log, and once in the tables). This is already
            // an overestimation, as most will delete an existing entry or
            // overwrite one. Still, use a conservative safety factor of 2.
            if (!blockFileManager.CheckDiskSpace(48 * 2 * 2 * CoinsTip().GetCacheSize()))
                return state.Error("out of disk space");
            // For a full flush, wipe the entire cache; for a prune flush, use Sync()
            // to write dirty entries while keeping the cache warm for faster block connection.
            if (fDoFullFlush ? !CoinsTip().Flush() : !CoinsTip().Sync()) {
                m_chainman.Notifications().ReportFatalError(FatalErrorReason::COINS_WRITE_FAILED, "");
                return state.Error("Failed to write to coin database");
            }
            m_last_flush = nNow;
            full_flush_completed = fDoFullFlush;
            TRACE5(utxocache, utxocache_flush,
                (int64_t)(GetTimeMicros() - nNow), // in microseconds (µs)
                (uint32_t)mode,
                (uint64_t)coins_count,
                (uint64_t)coins_mem_usage,
                (bool)fFlushForPrune);
        }
    }
    if (full_flush_completed) {
        // Update best block in wallet (so we can detect restored wallets).
        GetMainSignals().ChainStateFlushed(m_chain.GetLocator());
    }
    } catch (const std::runtime_error& e) {
        m_chainman.Notifications().ReportFatalError(FatalErrorReason::FLUSH_SYSTEM_ERROR, e.what());
        return state.Error(std::string("System error while flushing: ") + e.what());
    }
    return true;
}
