// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <verificationprogress.h>

#include <blockindex.h>

#include <algorithm>
#include <ctime>
#include <mutex>

void VerificationProgress::Set(const ChainTxData& data)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_data = data;
}

void VerificationProgress::Refresh(const CBlockIndex* pindexNew, const CBlockIndex* best_header)
{
    // Smallest sampling window, so the rate stays stable on a short chain.
    static constexpr int64_t MIN_CHAIN_TX_WINDOW{100};

    if (!pindexNew || pindexNew->nHeight == 0)
        return;

    const int64_t windowSize = std::max(MIN_CHAIN_TX_WINDOW,
        static_cast<int64_t>(pindexNew->nHeight) / 100);

    if (pindexNew->nHeight % windowSize != 0)
        return;

    const CBlockIndex* pWindow = pindexNew;
    for (int64_t i = 0; i < windowSize && pWindow->pprev; i++)
        pWindow = pWindow->pprev;

    const int64_t dt = pindexNew->GetBlockTime() - pWindow->GetBlockTime();
    if (dt <= 0)
        return;

    const double rate = static_cast<double>(
        static_cast<int64_t>(pindexNew->nChainTx) - static_cast<int64_t>(pWindow->nChainTx)) / dt;

    // The best header, not the tip, so that during IBD progress follows
    // height / best header height instead of extrapolating the empty time
    // since the last block.
    const int64_t bestHeight =
        (best_header && best_header->nHeight > pindexNew->nHeight)
        ? static_cast<int64_t>(best_header->nHeight)
        : static_cast<int64_t>(pindexNew->nHeight);

    const double txPerBlock = static_cast<double>(pindexNew->nChainTx) /
        pindexNew->nHeight;

    const int64_t nTxCount = std::max(
        static_cast<int64_t>(pindexNew->nChainTx),
        static_cast<int64_t>(txPerBlock * bestHeight));

    Set({time(nullptr), nTxCount, rate});
}

double VerificationProgress::Guess(const CBlockIndex* pindex) const
{
    if (pindex == nullptr)
        return 0.0;

    // TODO: nChainTx is read without cs_main, while ReceivedBlockTransactions writes
    // it under cs_main, e.g. for the header tip. Core requires cs_main here.
    // If nChainTx is 0, the block hasn't been fully validated/connected yet.
    // Return 0 to indicate no progress can be determined.
    if (pindex->nChainTx == 0)
        return 0.0;

    int64_t nNow = time(nullptr);

    std::shared_lock<std::shared_mutex> lock(m_mutex);
    double fTxTotal;

    if (pindex->nChainTx <= m_data.nTxCount) {
        fTxTotal = m_data.nTxCount + (nNow - m_data.nTime) * m_data.dTxRate;
    } else {
        fTxTotal = pindex->nChainTx + (nNow - pindex->GetBlockTime()) * m_data.dTxRate;
    }

    return pindex->nChainTx / fTxTotal;
}
