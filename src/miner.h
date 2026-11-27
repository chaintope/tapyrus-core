// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include <heightderivedconsensusrules.h>
#include <primitives/block.h>
#include <txmempool.h>
#include <validation.h>

#include <atomic>
#include <stdint.h>
#include <memory>
#include <utility>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>

class CBlockIndex;
class ArgsManager;
class ChainstateManager;
struct PolicySettings;
class CScript;

namespace Consensus { struct Params; };

static const bool DEFAULT_PRINTPRIORITY = false;

struct CBlockTemplate
{
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int32_t> vTxSigOpsCost;
    std::vector<unsigned char> vchCoinbaseCommitment;
};

// Container for tracking updates to ancestor feerate as we include (parent)
// transactions in a block
struct CTxMemPoolModifiedEntry {
    explicit CTxMemPoolModifiedEntry(CTxMemPool::txiter entry)
    {
        iter = entry;
        nSizeWithAncestors = entry->GetSizeWithAncestors();
        nModFeesWithAncestors = entry->GetModFeesWithAncestors();
        nSigOpCostWithAncestors = entry->GetSigOpCostWithAncestors();
    }

    int64_t GetModifiedFee() const { return iter->GetModifiedFee(); }
    uint64_t GetSizeWithAncestors() const { return nSizeWithAncestors; }
    CAmount GetModFeesWithAncestors() const { return nModFeesWithAncestors; }
    size_t GetTxSize() const { return iter->GetTxSize(); }
    const CTransaction& GetTx() const { return iter->GetTx(); }

    CTxMemPool::txiter iter;
    uint64_t nSizeWithAncestors;
    CAmount nModFeesWithAncestors;
    int32_t nSigOpCostWithAncestors;
};

/** Comparator for CTxMemPool::txiter objects.
 *  It simply compares the internal memory address of the CTxMemPoolEntry object
 *  pointed to. This means it has no meaning, and is only useful for using them
 *  as key in other indexes.
 */
struct CompareCTxMemPoolIter {
    bool operator()(const CTxMemPool::txiter& a, const CTxMemPool::txiter& b) const
    {
        return &(*a) < &(*b);
    }
};

struct modifiedentry_iter {
    typedef CTxMemPool::txiter result_type;
    result_type operator() (const CTxMemPoolModifiedEntry &entry) const
    {
        return entry.iter;
    }
};

// A comparator that sorts transactions based on number of ancestors.
// This is sufficient to sort an ancestor package in an order that is valid
// to appear in a block.
struct CompareTxIterByAncestorCount {
    bool operator()(const CTxMemPool::txiter &a, const CTxMemPool::txiter &b) const
    {
        if (a->GetCountWithAncestors() != b->GetCountWithAncestors())
            return a->GetCountWithAncestors() < b->GetCountWithAncestors();
        return CTxMemPool::CompareIteratorByHash()(a, b);
    }
};

typedef boost::multi_index_container<
    CTxMemPoolModifiedEntry,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<
            modifiedentry_iter,
            CompareCTxMemPoolIter
        >,
        // sorted by modified ancestor fee rate
        boost::multi_index::ordered_non_unique<
            // Reuse same tag from CTxMemPool's similar index
            boost::multi_index::tag<ancestor_score>,
            boost::multi_index::identity<CTxMemPoolModifiedEntry>,
            CompareTxMemPoolEntryByAncestorFee
        >
    >
> indexed_modified_transaction_set;

typedef indexed_modified_transaction_set::nth_index<0>::type::iterator modtxiter;
typedef indexed_modified_transaction_set::index<ancestor_score>::type::iterator modtxscoreiter;

struct update_for_parent_inclusion
{
    explicit update_for_parent_inclusion(CTxMemPool::txiter it) : iter(it) {}

    void operator() (CTxMemPoolModifiedEntry &e)
    {
        e.nModFeesWithAncestors -= iter->GetFee();
        e.nSizeWithAncestors -= iter->GetTxSize();
        e.nSigOpCostWithAncestors -= iter->GetSigOpCost();
    }

    CTxMemPool::txiter iter;
};


/** Generate a new block, without valid proof-of-work */
class BlockAssembler
{
public:
    /** What a block template is built with: the node options that control assembly and
     *  the next block's height and limits, read once from the active tip. */
    struct Options {
        /** Reads -blockmaxsize, -blockmintxfee and -blockfeatures from the policy settings,
         *  -printpriority from args, and the active tip and its next block's rules from chainman. */
        Options(const PolicySettings& policy, const ArgsManager& args, ChainstateManager& chainman);

        /** The tip the block builds on; CreateNewBlock refuses to build once it is no longer active. */
        CBlockIndex* const pindexPrev;
        const int nHeight;
        /** The tip's median time past; transactions must be final at it. */
        const int64_t nLockTimeCutoff;
        /** The size blocks are filled to: -blockmaxsize, kept between 1000 bytes and
         *  1000 bytes below the block size limit (room for the coinbase). */
        const size_t nBlockMaxSize;
        /** The next block's sigops limit. */
        const uint32_t nBlockMaxSigops;
        /** -blockmintxfee: packages paying less are left out. */
        const CFeeRate blockMinFeeRate;
        /** nFeatures of the blocks built: -blockfeatures in DEV mode, the default otherwise. */
        const int32_t nBlockFeatures;
        /** -printpriority: log the fee rate and txid of each transaction added. */
        const bool fPrintPriority;

    private:
        using TipAndRules = std::pair<CBlockIndex*, HeightDerivedConsensusRules>;
        /** The active tip and the rules of the block after it, read under cs_main. */
        static TipAndRules ReadTip(ChainstateManager& chainman);
        Options(const PolicySettings& policy, const ArgsManager& args, const TipAndRules& tip);
    };

private:
    ChainstateManager& m_chainman;
    //! The transactions the block is filled from.
    const CTxMemPool& m_mempool;
    const Options m_options;

    // The constructed block template
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    // A convenience pointer that always refers to the CBlock in pblocktemplate
    CBlock* pblock;
    // The mempool entries already in the block
    CTxMemPool::setEntries inBlock;

    /** The running totals of the block being filled, which the limits are checked
     *  against. CreateNewBlock holds one for the length of the call. */
    class BlockAssemblerStats
    {
    public:
        //! Starts with room for the header and the coinbase.
        uint64_t nBlockSize{1000};
        //! Not counting the coinbase.
        uint64_t nBlockTx{0};
        //! Starts with room for the coinbase's sigops.
        uint32_t nBlockSigOpsCost{100};
        //! Not counting the coinbase.
        CAmount nFees{0};
    };

public:
    //! Builds on chainman's active chain with its mempool, using the given options.
    BlockAssembler(ChainstateManager& chainman, const Options& options);
    //! As above, with options read from policy, args and chainman's tip.
    BlockAssembler(ChainstateManager& chainman, const PolicySettings& policy, const ArgsManager& args);

    /** Size and transaction count of the last block template built, for
     *  getmininginfo. An assembler lives for one template, so these outlive it;
     *  both are zero until the first template. */
    inline static std::atomic<uint64_t> m_last_block_size{0};
    inline static std::atomic<uint64_t> m_last_block_tx{0};

    /** Construct a new block template with coinbase to scriptPubKeyIn */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, int required_age_in_secs=0, CXField* pXField = nullptr);

private:
    // utility functions
    /** Add a tx to the block */
    void AddToBlock(BlockAssemblerStats& newBlockStats, CTxMemPool::txiter iter);

    // Methods for how to add transactions to a block.
    /** Add transactions based on feerate including unconfirmed ancestors
      * Increments nPackagesSelected / nDescendantsUpdated with corresponding
      * statistics from the package selection (for logging statistics). */
    void addPackageTxs(BlockAssemblerStats& newBlockStats, int &nPackagesSelected, int &nDescendantsUpdated, int required_age_in_secs=0) EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs);

    // helper functions for addPackageTxs()
    /** Remove confirmed (inBlock) entries from given set */
    void onlyUnconfirmed(CTxMemPool::setEntries& testSet) const;
    /** Test if a new package would "fit" in the block */
    bool TestPackage(const BlockAssemblerStats& newBlockStats, uint64_t packageSize, int32_t packageSigOpsCost) const;
    /** Perform checks on each transaction in a package:
      * locktime, serialized size (if necessary)
      * These checks should always succeed, and they're here
      * only as an extra check in case of suboptimal node configuration */
    bool TestPackageTransactions(const CTxMemPool::setEntries& package);
    /** Return true if given transaction from mapTx has already been evaluated,
      * or if the transaction's cached data in mapTx is incorrect. */
    bool SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx) EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs);
    /** Sort the package in an order that is valid to appear in a block */
    void SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries);
    /** Add descendants of given transactions to mapModifiedTx with ancestor
      * state updated assuming given transactions are inBlock. Returns number
      * of updated descendants. */
    int UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded, indexed_modified_transaction_set &mapModifiedTx) EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs);
};

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
int64_t UpdateTime(CBlockHeader* pblock, const CBlockIndex* pindexPrev);

#endif // BITCOIN_MINER_H
