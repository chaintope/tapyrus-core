// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CONTEXTUALCONSENSUSRULES_H
#define TAPYRUS_CONTEXTUALCONSENSUSRULES_H


#include <amount.h>

#include <cstdint>
#include <utility>
#include <vector>

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CCoinsViewCache;
class CTransaction;
class CValidationState;


/**
 * The consensus rules that need the chain or the coins, which are given as arguments.
 * The rules that need only a block's HeightDerivedConsensusRules are in tapyrus_consensus.
 * Holds nothing, so its methods are static.
 */
class ContextualConsensusRules
{
public:
    ContextualConsensusRules() = delete;

    /**
     * Header checks against the previous blocks: the last checkpoint (null when
     * checkpoints are off) and timestamps. Not invoked by ConnectBlock(), and
     * skipped by -reindex-chainstate.
     */
    static bool ContextualCheckBlockHeader(const CBlockHeader& header, CValidationState& state, const CBlockIndex& prev, int64_t nAdjustedTime, const CBlockIndex* lastCheckpoint);
    /** A coinbase records its block's height: the previous block's plus one. */
    static bool CheckCoinbaseHeight(const CBlock& block, CValidationState& state, const CBlockIndex& prev);
    /**
     * Block checks against the previous block: the coinbase height and that every
     * transaction is final. Not invoked by ConnectBlock(), and skipped by -reindex-chainstate.
     */
    static bool ContextualCheckBlock(const CBlock& block, CValidationState& state, const CBlockIndex& prev);

    /**
     * Check whether all inputs of this transaction are valid (no double spends and amounts)
     * This does not modify the UTXO set. This does not check scripts and sigs.
     * @param[out] txfee Set to the transaction fee if successful.
     * Preconditions: tx.IsCoinBase() is false.
     */
    static bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee);

    /**
     * Calculates the block height and previous block's median time past at
     * which the transaction will be considered final in the context of BIP 68.
     * Also removes from the vector of input heights any entries which did not
     * correspond to sequence locked inputs as they do not affect the calculation.
     */
    static std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block);
    static bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair);
    /**
     * Check if transaction is final per BIP 68 sequence numbers and can be included in a block.
     * Consensus critical. Takes as input a list of heights at which tx's inputs (in order) confirmed.
     */
    static bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block);

    /**
     * Count ECDSA signature operations in pay-to-script-hash inputs.
     * @return maximum number of sigops required to validate this transaction's inputs
     */
    static unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs);
    /** Total signature operation cost of a transaction, legacy and P2SH. */
    static uint32_t GetTransactionSigOps(const CTransaction& tx, const CCoinsViewCache& inputs, int flags);
};

#endif // TAPYRUS_CONTEXTUALCONSENSUSRULES_H
