// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>
#include <limits>
#include <map>
#include <string>

namespace Consensus {


/**
 * Parameters that influence chain consensus.
 */
struct Params {
    int nSubsidyHalvingInterval;
    /** Time for expected spacing between blocks (seconds)*/
    int64_t nExpectedBlockTime;
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
