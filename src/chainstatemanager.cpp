// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainstatemanager.h>

#include <blockindex.h>
#include <primitives/block.h>
#include <primitives/xfieldvalues.h>

#include <assert.h>
#include <limits>
#include <utility>

ChainstateManager g_chainstate_manager;

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
