// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainstatemanager.h>

#include <blockindex.h>
#include <util.h>
#include <primitives/block.h>
#include <primitives/xfieldvalues.h>
#include <txdb.h>

#include <assert.h>
#include <limits>
#include <utility>

ChainstateManager g_chainstate_manager;

ChainstateManager::Options::Options(const ArgsManager& args, bool defaultConsistencyChecks, size_t coinCacheUsage)
    : fReindexChainState(args.GetBoolArg("-reindex-chainstate", false)),
      fReloadXField(args.GetBoolArg("-reloadxfield", false)),
      fCheckBlockIndex(args.GetBoolArg("-checkblockindex", defaultConsistencyChecks)),
      fCheckpointsEnabled(args.GetBoolArg("-checkpoints", DEFAULT_CHECKPOINTS_ENABLED)),
      hashAssumeValid(uint256S(args.GetArg("-assumevalid", ""))),
      nMaxTipAge(args.GetArg("-maxtipage", DEFAULT_MAX_TIP_AGE)),
      nScriptCheckThreads(ParseScriptCheckThreads(args)),
      nCoinCacheUsage(coinCacheUsage)
{
}

int ChainstateManager::Options::ParseScriptCheckThreads(const ArgsManager& args)
{
    // -par=0 means autodetect, but nScriptCheckThreads==0 means no concurrency
    int nScriptCheckThreads = args.GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (nScriptCheckThreads <= 0)
        nScriptCheckThreads += GetNumCores();
    if (nScriptCheckThreads <= 1)
        nScriptCheckThreads = 0;
    else if (nScriptCheckThreads > MAX_SCRIPTCHECK_THREADS)
        nScriptCheckThreads = MAX_SCRIPTCHECK_THREADS;
    return nScriptCheckThreads;
}

void ChainstateManager::InitOptions(const Options& options)
{
    m_options = std::make_unique<const Options>(options);
}

void ChainstateManager::InitXFieldHistory(const CBlock& genesis)
{
    m_xfield_history.Init(genesis);
}

void ChainstateManager::InitConsensusSettings(std::unique_ptr<const ConsensusSettings> settings)
{
    m_consensus_settings = std::move(settings);
}

const ConsensusSettings& ChainstateManager::Settings() const
{
    assert(m_consensus_settings);
    return *m_consensus_settings;
}

HeightDerivedConsensusRules ChainstateManager::RulesAt(const CBlockIndex& block) const
{
    return RulesAt(block, m_xfield_history);
}

HeightDerivedConsensusRules ChainstateManager::RulesAt(const CBlockIndex& block, const CXFieldHistoryMap& history) const
{
    return BuildRules(block.nHeight, history);
}

HeightDerivedConsensusRules ChainstateManager::RulesAfter(const CBlockIndex& prev) const
{
    return RulesAfter(prev, m_xfield_history);
}

HeightDerivedConsensusRules ChainstateManager::RulesAfter(const CBlockIndex& prev, const CXFieldHistoryMap& history) const
{
    return BuildRules(prev.nHeight + 1, history);
}

HeightDerivedConsensusRules ChainstateManager::RulesLatest() const
{
    return RulesLatest(m_xfield_history);
}

HeightDerivedConsensusRules ChainstateManager::RulesLatest(const CXFieldHistoryMap& history) const
{
    return BuildRules(std::nullopt, history);
}

HeightDerivedConsensusRules ChainstateManager::BuildRules(std::optional<int32_t> height, const CXFieldHistoryMap& history) const
{
    // An unknown height reads the latest entries.
    const uint32_t lookup = height ? static_cast<uint32_t>(*height) : std::numeric_limits<uint32_t>::max();
    const XFieldAggPubKey aggregatePubKey = std::get<XFieldAggPubKey>(history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, lookup).xfieldValue);
    const XFieldMaxBlockSize maxBlockSize = std::get<XFieldMaxBlockSize>(history.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, lookup).xfieldValue);
    // A soft fork with no registration on this network is active from genesis.
    const int32_t flagsHeight = height ? *height : std::numeric_limits<int32_t>::max();
    return HeightDerivedConsensusRules(height, maxBlockSize.data, aggregatePubKey.getPubKey(), Settings().ScriptFlagsAt(flagsHeight));
}

void ChainstateManager::InitBlockFileManager(const BlockFileManager::Options& options)
{
    m_block_file_manager = std::make_unique<BlockFileManager>(options, m_block_index);
}

bool ChainstateManager::ShouldWipeBlockTree()
{
    return m_reindexing || GetOptions().fReloadXField;
}

bool ChainstateManager::LoadingBlocks()
{
    return GetBlockFileManager().IsImporting() || m_reindexing;
}

bool ChainstateManager::ReadBlock(CBlock& block, const CBlockIndex* pindex)
{
    return GetBlockFileManager().ReadBlockFromDisk(block, pindex, RulesAt(*pindex));
}

bool ChainstateManager::IsBlockPruned(const CBlockIndex* pblockindex) const
{
    return (m_have_pruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0);
}

void ChainstateManager::RecordPruned()
{
    if (!m_have_pruned) {
        m_block_index.pblocktreedb->WriteFlag("prunedblockfiles", true);
        m_have_pruned = true;
    }
}

void ChainstateManager::LoadPersistentFlags()
{
    CBlockTreeDB& blocktree = *m_block_index.pblocktreedb;

    // Check whether we have ever pruned block & undo files
    blocktree.ReadFlag("prunedblockfiles", m_have_pruned);
    if (m_have_pruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    blocktree.ReadReindexing(fReindexing);
    if (fReindexing) m_reindexing = true;
}
