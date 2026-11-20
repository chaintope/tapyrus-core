// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_MEMPOOLACCEPT_H
#define TAPYRUS_MEMPOOLACCEPT_H

#include <amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>

#include <cstdint>
#include <vector>

/** What an AcceptToMemoryPool call is asked to do. */
struct MempoolAcceptOptions {
    //! When the transaction entered a pool before; not used yet, as acceptance records the current time.
    int64_t accept_time{0};
    //! Skips the fee and size limits, for transactions a reorg returns to the pool.
    bool bypass_limits{false};
    //! Checks the transaction without adding it.
    bool test_only{false};
    //! Rejects a transaction paying a higher fee; 0 sets no limit.
    CAmount max_tx_fee{0};
};

/** What an AcceptToMemoryPool call found. */
struct MempoolAcceptResult {
    //! Why the transaction was rejected; valid if it was accepted or only its inputs are missing.
    CValidationState state;
    //! The pool transactions it replaced.
    std::vector<CTransactionRef> replaced;
    //! Its inputs that are neither in the chain nor in the pool.
    std::vector<COutPoint> missing_inputs;
};

#endif // TAPYRUS_MEMPOOLACCEPT_H
