// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/chain.h>

#include <blockindex.h>
#include <chain.h>
#include <chainstate.h>
#include <chainstatemanager.h>
#include <connman.h>
#include <consensus/validation.h>
#include <mempoolaccept.h>
#include <net_processing.h>
#include <nodecontext.h>
#include <policy/fees.h>
#include <policy/policysettings.h>
#include <policy/rbf.h>
#include <primitives/block.h>
#include <pubkey.h>
#include <txmempool.h>
#include <utilmemory.h>
#include <validation.h>
#include <validationinterface.h>
#include <xfieldhistory.h>

namespace interfaces {
namespace {

class ChainImpl : public Chain
{
public:
    explicit ChainImpl(NodeContext& node) : m_node(node) {}

    CFeeRate estimateSmartFee(int num_blocks, bool conservative, FeeCalculation* calc) const override
    {
        return m_node.fee_estimator->estimateSmartFee(num_blocks, calc, conservative);
    }

    unsigned int estimateMaxBlocks() const override
    {
        return m_node.fee_estimator->HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    }

    CFeeRate mempoolMinFee() const override
    {
        return m_node.mempool->GetMinFee();
    }

    bool hasDescendantsInMempool(const uint256& txid) const override
    {
        LOCK(m_node.mempool->cs);
        auto it = m_node.mempool->mapTx.find(txid);
        return it != m_node.mempool->mapTx.end() && it->GetCountWithDescendants() > 1;
    }

    void getTransactionAncestry(const uint256& txid, size_t& ancestors, size_t& descendants) const override
    {
        m_node.mempool->GetTransactionAncestry(txid, ancestors, descendants);
    }

    bool checkChainLimits(const CTransactionRef& tx) const override
    {
        LockPoints lp;
        CTxMemPoolEntry entry(tx, 0, 0, 0, false, 0, lp);
        CTxMemPool::setEntries ancestors;
        std::string error;
        LOCK(m_node.mempool->cs);
        return m_node.mempool->CalculateMemPoolAncestors(entry, ancestors, g_policy_settings->ancestorLimit, g_policy_settings->ancestorSizeLimit,
                                                         g_policy_settings->descendantLimit, g_policy_settings->descendantSizeLimit, error);
    }

    RBFTransactionState isRBFOptIn(const CTransaction& tx) const override
    {
        LOCK(m_node.mempool->cs);
        return IsRBFOptIn(tx, *m_node.mempool);
    }

    bool broadcastTransaction(const CTransactionRef& tx, CAmount max_tx_fee, bool relay, CValidationState& state) override
    {
        LOCK(::cs_main);
        if (!m_node.mempool->exists(tx->GetHashMalFix())) {
            MempoolAcceptOptions options;
            options.max_tx_fee = max_tx_fee;
            MempoolAcceptResult result;
            if (!g_chainstate_manager.ActiveChainstate().AcceptToMemoryPool(tx, options, result)) {
                state = result.state;
                return false;
            }
        }
        if (relay && m_node.connman) {
            RelayTransaction(*tx, m_node.connman.get());
        }
        return true;
    }

    WalletNotifications& notifications() const override
    {
        return m_node.notifications;
    }

    bool isReadyToBroadcast() override
    {
        return !g_chainstate_manager.LoadingBlocks() && !IsInitialBlockDownload();
    }

    std::optional<int> getHeight() override
    {
        LOCK(cs_main);
        const int height = ActiveChain().Height();
        if (height >= 0) return height;
        return std::nullopt;
    }

    uint256 getBlockHash(int height) override
    {
        LOCK(cs_main);
        const CBlockIndex* block = ActiveChain()[height];
        assert(block);
        return block->GetBlockHash();
    }

    bool haveBlockOnDisk(int height) override
    {
        LOCK(cs_main);
        const CBlockIndex* block = ActiveChain()[height];
        return block && (block->nStatus & BLOCK_HAVE_DATA) != 0 && block->nTx > 0;
    }

    CBlockLocator getTipLocator() override
    {
        LOCK(cs_main);
        return ActiveChain().GetLocator();
    }

    std::optional<int> findLocatorFork(const CBlockLocator& locator) override
    {
        LOCK(cs_main);
        if (const CBlockIndex* fork = FindForkInGlobalIndex(ActiveChain(), locator)) {
            return fork->nHeight;
        }
        return std::nullopt;
    }

    bool checkFinalTx(const CTransaction& tx) override
    {
        LOCK(cs_main);
        return CheckFinalTx(tx);
    }

    bool findBlock(const uint256& hash, const FoundBlock& block) override
    {
        const CBlockIndex* index;
        {
            LOCK(cs_main);
            index = g_chainstate_manager.BlockIndex().LookupBlockIndex(hash);
            if (!FillIndexData(index, block)) return false;
        }
        FillBlockData(index, block);
        return true;
    }

    bool findNextBlock(const uint256& block_hash, int block_height, const FoundBlock& next, bool* reorg) override
    {
        const CBlockIndex* index;
        {
            LOCK(cs_main);
            const CBlockIndex* block = ActiveChain()[block_height];
            if (block && block->GetBlockHash() != block_hash) block = nullptr;
            if (reorg) *reorg = !block;
            index = block ? ActiveChain()[block_height + 1] : nullptr;
            if (!FillIndexData(index, next)) return false;
        }
        FillBlockData(index, next);
        return true;
    }

    bool findFirstBlockWithTimeAndHeight(int64_t min_time, int min_height, const FoundBlock& block) override
    {
        const CBlockIndex* index;
        {
            LOCK(cs_main);
            index = ActiveChain().FindEarliestAtLeast(min_time, min_height);
            if (!FillIndexData(index, block)) return false;
        }
        FillBlockData(index, block);
        return true;
    }

    bool findAncestorByHeight(const uint256& block_hash, int ancestor_height, const FoundBlock& ancestor_out) override
    {
        const CBlockIndex* ancestor = nullptr;
        {
            LOCK(cs_main);
            if (const CBlockIndex* block = g_chainstate_manager.BlockIndex().LookupBlockIndex(block_hash)) {
                ancestor = block->GetAncestor(ancestor_height);
            }
            if (!FillIndexData(ancestor, ancestor_out)) return false;
        }
        FillBlockData(ancestor, ancestor_out);
        return true;
    }

    bool findAncestorByHash(const uint256& block_hash, const uint256& ancestor_hash, const FoundBlock& ancestor_out) override
    {
        const CBlockIndex* ancestor;
        {
            LOCK(cs_main);
            const CBlockIndex* block = g_chainstate_manager.BlockIndex().LookupBlockIndex(block_hash);
            ancestor = g_chainstate_manager.BlockIndex().LookupBlockIndex(ancestor_hash);
            if (block && ancestor && block->GetAncestor(ancestor->nHeight) != ancestor) ancestor = nullptr;
            if (!FillIndexData(ancestor, ancestor_out)) return false;
        }
        FillBlockData(ancestor, ancestor_out);
        return true;
    }

    bool findCommonAncestor(const uint256& block_hash1, const uint256& block_hash2, const FoundBlock& ancestor_out, const FoundBlock& block1_out, const FoundBlock& block2_out) override
    {
        const CBlockIndex* block1;
        const CBlockIndex* block2;
        const CBlockIndex* ancestor;
        bool found;
        {
            LOCK(cs_main);
            block1 = g_chainstate_manager.BlockIndex().LookupBlockIndex(block_hash1);
            block2 = g_chainstate_manager.BlockIndex().LookupBlockIndex(block_hash2);
            ancestor = block1 && block2 ? LastCommonAncestor(block1, block2) : nullptr;
            // Fill all three, even if the ancestor is missing.
            found = FillIndexData(ancestor, ancestor_out) & FillIndexData(block1, block1_out) & FillIndexData(block2, block2_out);
        }
        FillBlockData(ancestor, ancestor_out);
        FillBlockData(block1, block1_out);
        FillBlockData(block2, block2_out);
        return found;
    }

    bool hasBlocks(const uint256& block_hash, int min_height, std::optional<int> max_height) override
    {
        // hasBlocks returns true if all ancestors of block_hash in specified
        // range have block data (are not pruned), false if any ancestors in
        // specified range are missing data.
        //
        // For simplicity and robustness, min_height and max_height are only
        // used to limit the range, and passing min_height that's too low or
        // max_height that's too high will not crash or change the result.
        LOCK(cs_main);
        if (const CBlockIndex* block = g_chainstate_manager.BlockIndex().LookupBlockIndex(block_hash)) {
            if (max_height && block->nHeight >= *max_height) block = block->GetAncestor(*max_height);
            for (; block->nStatus & BLOCK_HAVE_DATA; block = block->pprev) {
                // Check pprev to not segfault if min_height is too low
                if (block->nHeight <= min_height || !block->pprev) return true;
            }
        }
        return false;
    }

    double guessVerificationProgress(const uint256& block_hash) override
    {
        LOCK(cs_main);
        return g_chainstate_manager.Progress().Guess(g_chainstate_manager.BlockIndex().LookupBlockIndex(block_hash));
    }

    bool havePruned() override
    {
        LOCK(cs_main);
        return g_chainstate_manager.HavePruned();
    }

    bool isPruneMode() override
    {
        return g_chainstate_manager.GetBlockFileManager().GetOptions().fPruneMode;
    }

    bool isInitialBlockDownload() override
    {
        return IsInitialBlockDownload();
    }

    void waitForNotificationsIfTipChanged(const uint256& old_tip) override
    {
        if (!old_tip.IsNull()) {
            LOCK(cs_main);
            if (const CBlockIndex* tip = ActiveChain().Tip(); tip && old_tip == tip->GetBlockHash()) return;
        }
        SyncWithValidationInterfaceQueue();
    }

    uint32_t getMaxBlockSize() override
    {
        return g_chainstate_manager.XFieldHistory().GetCurrentMaxBlockSize();
    }

    CPubKey getAggregatePubkey() override
    {
        XFieldAggPubKey aggpubkey;
        g_chainstate_manager.XFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkey);
        return aggpubkey.getPubKey();
    }

private:
    CChain& ActiveChain() EXCLUSIVE_LOCKS_REQUIRED(cs_main) { return g_chainstate_manager.ActiveChain(); }

    //! Fills what the index entry holds; false if there is no entry.
    bool FillIndexData(const CBlockIndex* index, const FoundBlock& block) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
    {
        if (!index) return false;
        if (block.m_hash) *block.m_hash = index->GetBlockHash();
        if (block.m_height) *block.m_height = index->nHeight;
        if (block.m_time) *block.m_time = index->GetBlockTime();
        if (block.m_max_time) *block.m_max_time = index->GetBlockTimeMax();
        if (block.m_mtp_time) *block.m_mtp_time = index->GetMedianTimePast();
        if (block.m_in_active_chain) *block.m_in_active_chain = ActiveChain()[index->nHeight] == index;
        return true;
    }

    //! Reads the block from disk if asked for, without cs_main, so the node
    //! never waits on the read; a block without data reads as null.
    void FillBlockData(const CBlockIndex* index, const FoundBlock& block) LOCKS_EXCLUDED(cs_main)
    {
        if (index && block.m_data && !g_chainstate_manager.ReadBlock(*block.m_data, index)) block.m_data->SetNull();
    }

    NodeContext& m_node;
};

} // namespace

std::unique_ptr<Chain> MakeChain(NodeContext& node) { return MakeUnique<ChainImpl>(node); }

} // namespace interfaces
