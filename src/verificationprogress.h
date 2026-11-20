// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_VERIFICATIONPROGRESS_H
#define TAPYRUS_VERIFICATIONPROGRESS_H

#include <cs_main.h>
#include <sync.h>

#include <cstdint>
#include <shared_mutex>

class CBlockIndex;

/**
 * Holds various statistics on transactions within a chain. Used to estimate
 * verification progress during chain sync.
 */
struct ChainTxData {
    int64_t nTime{0};    //!< UNIX timestamp of last known number of transactions
    int64_t nTxCount{0}; //!< total number of transactions between genesis and that timestamp
    double dTxRate{0};   //!< estimated number of transactions per second after that timestamp
};

/**
 * Estimates how far chain sync has got from the chain's transaction statistics.
 * The tip update rewrites the statistics about a hundred times over a sync,
 * while the GUI, RPC and the wallet read them for every block, so they have
 * their own reader-writer lock instead of cs_main.
 */
class VerificationProgress
{
public:
    /** Sets the statistics; init seeds them from the genesis block. */
    void Set(const ChainTxData& data);
    /** Re-estimates the statistics from the blocks before pindexNew, every 1% of
     *  the chain height (at least 100 blocks); the tip update calls it. */
    void Refresh(const CBlockIndex* pindexNew, const CBlockIndex* best_header) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Progress at pindex, from 0.0 (genesis) to 1.0 (current tip); 0.0 for a null
     *  block or one not connected yet. The lock covers only the statistics: the caller
     *  holds cs_main, or knows pindex is connected, since the chain writes its nChainTx. */
    double Guess(const CBlockIndex* pindex) const;

private:
    mutable std::shared_mutex m_mutex;
    ChainTxData m_data;
};

#endif // TAPYRUS_VERIFICATIONPROGRESS_H
