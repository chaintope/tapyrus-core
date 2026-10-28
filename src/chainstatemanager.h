// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CHAINSTATEMANAGER_H
#define TAPYRUS_CHAINSTATEMANAGER_H

#include <consensusinterface.h>
#include <xfieldhistory.h>

class CBlock;

/**
 * Owns the node's chain-wide state, as Bitcoin Core's ChainstateManager does.
 * So far it holds the xfield history of the active chain and the consensus interface.
 */
class ChainstateManager
{
public:
    /** Adds the genesis block's xfield entries; safe to call again (entries already present are kept). */
    void InitXFieldHistory(const CBlock& genesis);

    CXFieldHistory& XFieldHistory() { return m_xfield_history; }

    ConsensusInterface& Consensus() { return m_consensus; }

private:
    CXFieldHistory m_xfield_history;
    //! Reads m_xfield_history, so it is declared after it.
    ConsensusInterface m_consensus{m_xfield_history};
};

extern ChainstateManager g_chainstate_manager;

#endif // TAPYRUS_CHAINSTATEMANAGER_H
