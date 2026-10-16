// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainstatemanager.h>

#include <primitives/block.h>

ChainstateManager g_chainstate_manager;

void ChainstateManager::InitXFieldHistory(const CBlock& genesis)
{
    m_xfield_history.Init(genesis);
}
