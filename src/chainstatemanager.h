// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CHAINSTATEMANAGER_H
#define TAPYRUS_CHAINSTATEMANAGER_H

#include <consensussettings.h>
#include <cs_main.h>
#include <heightderivedconsensusrules.h>
#include <issuedcolorids.h>
#include <xfieldhistory.h>

#include <cstdint>
#include <memory>
#include <optional>

class CBlock;
class CBlockIndex;

/**
 * Owns the node's chain-wide state, as Bitcoin Core's ChainstateManager does.
 * So far it holds the xfield history and the issued colorIds of the active chain,
 * and the consensus settings.
 */
class ChainstateManager
{
public:
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

private:
    HeightDerivedConsensusRules BuildRules(std::optional<int32_t> height, const CXFieldHistoryMap& history) const;

    CXFieldHistory m_xfield_history;
    CIssuedColorIds m_issued_colorids GUARDED_BY(cs_main);
    std::unique_ptr<const ConsensusSettings> m_consensus_settings;
};

extern ChainstateManager g_chainstate_manager;

#endif // TAPYRUS_CHAINSTATEMANAGER_H
