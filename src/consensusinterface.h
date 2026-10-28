// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CONSENSUSINTERFACE_H
#define TAPYRUS_CONSENSUSINTERFACE_H

#include <consensussettings.h>
#include <heightderivedconsensusrules.h>

#include <cstdint>
#include <memory>

class CBlockIndex;
class CXFieldHistoryMap;

/**
 * The node's access to the consensus rules: it holds the ConsensusSettings and
 * builds the HeightDerivedConsensusRules of a block from the xfield history. Owned by
 * ChainstateManager. The history defaults to the node's; the reindex path
 * passes its working copy, which may run ahead of the tip.
 */
class ConsensusInterface
{
public:
    explicit ConsensusInterface(const CXFieldHistoryMap& history) : m_history(history) {}

    /** Sets the settings once at start-up; tests set them again for each fixture. */
    void Init(std::unique_ptr<const ConsensusSettings> settings) { m_settings = std::move(settings); }

    const ConsensusSettings& Settings() const;

    /** The rules of this block. */
    HeightDerivedConsensusRules RulesAt(const CBlockIndex& block, const CXFieldHistoryMap* history = nullptr) const;
    /** The rules of the next block on prev. */
    HeightDerivedConsensusRules RulesAfter(const CBlockIndex& prev, const CXFieldHistoryMap* history = nullptr) const;
    /**
     * The rules of the history's latest entries, for a block whose position is
     * not known. For the node's history this equals RulesAfter(tip); every
     * registered soft fork counts as active.
     */
    HeightDerivedConsensusRules RulesLatest(const CXFieldHistoryMap* history = nullptr) const;

    /** Whether the soft fork with this script flag is active at this height. */
    bool IsActive(uint32_t flag, int32_t height) const;
    /** The soft-fork script flags that activate between these heights, or 0. */
    uint32_t ActivationFlagsChange(int32_t oldHeight, int32_t newHeight) const;

private:
    HeightDerivedConsensusRules Rules(std::optional<int32_t> height, const CXFieldHistoryMap* history) const;

    const CXFieldHistoryMap& m_history;
    std::unique_ptr<const ConsensusSettings> m_settings;
};

#endif // TAPYRUS_CONSENSUSINTERFACE_H
