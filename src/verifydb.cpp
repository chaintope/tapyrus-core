// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <verifydb.h>

#include <chainstate.h>
#include <chainstatemanager.h>
#include <checkblock.h>
#include <validation.h>
#include <shutdown.h>
#include <ui_interface.h>
#include <blockfilemanager.h>
#include <issuedcolorids.h>

#include <optional>

CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0, false);
}

CVerifyDB::~CVerifyDB()
{
    uiInterface.ShowProgress("", 100, false);
}

bool CVerifyDB::VerifyDB(CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    return VerifyDB(g_chainstate_manager.ActiveChainstate(), coinsview, nCheckLevel, nCheckDepth);
}

bool CVerifyDB::VerifyDB(Chainstate& chainstate, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    AssertLockHeld(cs_main);
    ChainstateManager& chainman = chainstate.Chainman();
    if (chainstate.m_chain.Tip() == nullptr || chainstate.m_chain.Tip()->pprev == nullptr)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0 || nCheckDepth > chainstate.m_chain.Height())
        nCheckDepth = chainstate.m_chain.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindex;
    CBlockIndex* pindexFailure = nullptr;
    int nGoodTransactions = 0;
    CValidationState state;
    int reportDone = 0;

    // Levels 3 and 4 disconnect and reconnect blocks on a copy of the issued colorIds,
    // as they do on a local coins cache, so the live set is never changed.
    const std::unique_ptr<CIssuedColorIds> issuedColorIds = nCheckLevel >= 3 ? chainman.IssuedColorIds().Clone() : nullptr;

    LogPrintf("[0%%]..."); /* Continued */
    for (pindex = chainstate.m_chain.Tip(); pindex && pindex->pprev; pindex = pindex->pprev) {
        int percentageDone = std::max(1, std::min(99, (int)(((double)(chainstate.m_chain.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));
        if (reportDone < percentageDone/10) {
            // report every 10% step
            LogPrintf("[%d%%]...", percentageDone); /* Continued */
            reportDone = percentageDone/10;
        }
        uiInterface.ShowProgress(_("Verifying blocks..."), percentageDone, false);
        if (pindex->nHeight <= chainstate.m_chain.Height()-nCheckDepth)
            break;
        if (chainman.GetBlockFileManager().GetOptions().fPruneMode && !(pindex->nStatus & BLOCK_HAVE_DATA)) {
            // If pruning, only go back as far as we have data.
            LogPrintf("VerifyDB(): block verification stopping at height %d (pruning, no data)\n", pindex->nHeight);
            break;
        }
        CBlock block;
        // check level 0: read from disk
        if (!chainman.ReadBlock(block, pindex))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state, chainman.RulesAt(*pindex), true, true))
            return error("%s: *** found bad block at %d, hash=%s (%s)\n", __func__,
                         pindex->nHeight, pindex->GetBlockHash().ToString(), FormatStateMessage(state));
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            if (!pindex->GetUndoPos().IsNull()) {
                if (!chainman.GetBlockFileManager().UndoReadFromDisk(undo, pindex, block)) {
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                }
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && (coins.DynamicMemoryUsage() + chainstate.CoinsTip().DynamicMemoryUsage()) <= chainman.GetOptions().nCoinCacheUsage) {
            assert(coins.GetBestBlock() == pindex->GetBlockHash());
            // Erases from the copy, so level 4 can add the colorIds again when it reconnects.
            DisconnectResult res = chainstate.DisconnectBlock(block, pindex, coins, *issuedColorIds);
            if (res == DISCONNECT_FAILED) {
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
            if (res == DISCONNECT_UNCLEAN) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else {
                nGoodTransactions += block.vtx.size();
            }
        }
        if (ShutdownRequested()) {
            LogPrintf("[ABORTED].\n");
            LogPrintf("VerifyDB(): interrupted by shutdown at height %d — level-%i verification incomplete\n",
                      pindex->nHeight, nCheckLevel);
            return true;
        }
    }
    if (pindexFailure)
        return error("VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", chainstate.m_chain.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // store block count as we move pindex at check level >= 4
    int block_count = chainstate.m_chain.Height() - pindex->nHeight;

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        while (pindex != chainstate.m_chain.Tip()) {
            if (ShutdownRequested()) {
                LogPrintf("[ABORTED].\n");
                LogPrintf("VerifyDB(): interrupted by shutdown at height %d — level-4 reconnect incomplete\n",
                          pindex->nHeight);
                return true;
            }
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainstate.m_chain.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))), false);
            pindex = chainstate.m_chain.Next(pindex);
            CBlock block;
            if (!chainman.ReadBlock(block, pindex))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            if (!chainstate.ConnectBlock(block, state, pindex, coins, *issuedColorIds))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s (%s)", pindex->nHeight, pindex->GetBlockHash().ToString(), FormatStateMessage(state));
        }
    }

    LogPrintf("[DONE].\n");
    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", block_count, nGoodTransactions);

    return true;
}
