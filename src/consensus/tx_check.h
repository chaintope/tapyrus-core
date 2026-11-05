// Copyright (c) 2017-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CONSENSUS_TX_CHECK_H
#define TAPYRUS_CONSENSUS_TX_CHECK_H

#include <cstdint>

class CTransaction;
class CValidationState;

/** Context-independent validity checks; maxBlockSize is the block size limit the transaction must fit in */
bool CheckTransaction(const CTransaction& tx, CValidationState& state, uint32_t maxBlockSize, bool fCheckDuplicateInputs=true);
/** Catches calls still written for the old signature, whose bool would silently become a zero size limit */
bool CheckTransaction(const CTransaction& tx, CValidationState& state, bool fCheckDuplicateInputs, bool unused=true) = delete;

/**
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way
 * @return number of sigops this transaction's outputs will produce when spent
 */
unsigned int GetLegacySigOpCount(const CTransaction& tx);

#endif // TAPYRUS_CONSENSUS_TX_CHECK_H
