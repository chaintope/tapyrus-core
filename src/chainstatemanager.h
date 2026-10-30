// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CHAINSTATEMANAGER_H
#define TAPYRUS_CHAINSTATEMANAGER_H

#include <consensusinterface.h>
#include <cs_main.h>
#include <issuedcolorids.h>
#include <xfieldhistory.h>

class CBlock;

/**
 * Owns the node's chain-wide state, as Bitcoin Core's ChainstateManager does.
 * So far it holds the xfield history and the issued colorIds of the active chain,
 * and the consensus interface.
 */
class ChainstateManager
{
public:
    /** Adds the genesis block's xfield entries; safe to call again (entries already present are kept). */
    void InitXFieldHistory(const CBlock& genesis);

    CXFieldHistory& XFieldHistory() { return m_xfield_history; }

    ConsensusInterface& Consensus() { return m_consensus; }

    /** The NON_REISSUABLE and NFT colorIds issued on the active chain; they change with the tip. */
    CIssuedColorIds& IssuedColorIds() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return m_issued_colorids; }

private:
    CXFieldHistory m_xfield_history;
    CIssuedColorIds m_issued_colorids GUARDED_BY(cs_main);
    //! Reads m_xfield_history, so it is declared after it.
    ConsensusInterface m_consensus{m_xfield_history};
};

extern ChainstateManager g_chainstate_manager;

#endif // TAPYRUS_CHAINSTATEMANAGER_H
