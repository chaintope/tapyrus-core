// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensusinterface.h>

#include <blockindex.h>
#include <primitives/xfieldvalues.h>
#include <script/interpreter.h>
#include <xfieldhistorymap.h>

#include <assert.h>
#include <limits>

const ConsensusSettings& ConsensusInterface::Settings() const
{
    assert(m_settings);
    return *m_settings;
}

HeightDerivedConsensusRules ConsensusInterface::RulesAt(const CBlockIndex& block, const CXFieldHistoryMap* history) const
{
    return Rules(block.nHeight, history);
}

HeightDerivedConsensusRules ConsensusInterface::RulesAfter(const CBlockIndex& prev, const CXFieldHistoryMap* history) const
{
    return Rules(prev.nHeight + 1, history);
}

HeightDerivedConsensusRules ConsensusInterface::RulesLatest(const CXFieldHistoryMap* history) const
{
    return Rules(std::nullopt, history);
}

HeightDerivedConsensusRules ConsensusInterface::Rules(std::optional<int32_t> height, const CXFieldHistoryMap* history) const
{
    const CXFieldHistoryMap& xfieldHistory = history ? *history : m_history;
    // An unknown height reads the latest entries.
    const uint32_t lookup = height ? static_cast<uint32_t>(*height) : std::numeric_limits<uint32_t>::max();
    const XFieldAggPubKey aggregatePubKey = std::get<XFieldAggPubKey>(xfieldHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, lookup).xfieldValue);
    const XFieldMaxBlockSize maxBlockSize = std::get<XFieldMaxBlockSize>(xfieldHistory.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, lookup).xfieldValue);
    // A soft fork with no registration on this network is active from genesis.
    const int32_t flagsHeight = height ? *height : std::numeric_limits<int32_t>::max();
    uint32_t scriptFlags = SCRIPT_VERIFY_NONE;
    if (IsActive(SCRIPT_VERIFY_CP2SH_COLORED, flagsHeight))
        scriptFlags |= SCRIPT_VERIFY_CP2SH_COLORED;
    return HeightDerivedConsensusRules(height, maxBlockSize.data, aggregatePubKey.getPubKey(), scriptFlags);
}

bool ConsensusInterface::IsActive(uint32_t flag, int32_t height) const
{
    return Settings().SoftForkManager().IsActive(Settings().NetworkId(), flag, height);
}

uint32_t ConsensusInterface::ActivationFlagsChange(int32_t oldHeight, int32_t newHeight) const
{
    return Settings().SoftForkManager().ActivationFlagsChange(Settings().NetworkId(), oldHeight, newHeight);
}
