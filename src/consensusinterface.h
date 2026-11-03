// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CONSENSUSINTERFACE_H
#define TAPYRUS_CONSENSUSINTERFACE_H

#include <consensussettings.h>
#include <heightderivedconsensusrules.h>

#include <amount.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CCoinsViewCache;
class CTransaction;
class CValidationState;
class CXFieldHistoryMap;


/**
 * The node's access to the consensus rules: it holds the ConsensusSettings and
 * builds the HeightDerivedConsensusRules of a block from the xfield history. Owned by
 * ChainstateManager. The history defaults to the node's; the reindex path
 * passes its working copy, which may run ahead of the tip.
 */
class ConsensusInterface
{
public:
    explicit ConsensusInterface(const CXFieldHistoryMap& history) : m_history(history) {}

    /** Sets the settings once at start-up; tests set them again for each fixture. */
    void Init(std::unique_ptr<const ConsensusSettings> settings) { m_settings = std::move(settings); }

    const ConsensusSettings& Settings() const;

    /** The rules of this block. */
    HeightDerivedConsensusRules RulesAt(const CBlockIndex& block, const CXFieldHistoryMap* history = nullptr) const;
    /** The rules of the next block on prev. */
    HeightDerivedConsensusRules RulesAfter(const CBlockIndex& prev, const CXFieldHistoryMap* history = nullptr) const;
    /**
     * The rules of the history's latest entries, for a block whose position is
     * not known. For the node's history this equals RulesAfter(tip); every
     * registered soft fork counts as active.
     */
    HeightDerivedConsensusRules RulesLatest(const CXFieldHistoryMap* history = nullptr) const;

    /** Whether the soft fork with this script flag is active at this height. */
    bool IsActive(uint32_t flag, int32_t height) const;
    /** The soft-fork script flags that activate between these heights, or 0. */
    uint32_t ActivationFlagsChange(int32_t oldHeight, int32_t newHeight) const;

    // The consensus rules that need the chain or the coins, which they are given
    // as arguments. The rules that need only a block's HeightDerivedConsensusRules are in
    // tapyrus_consensus.

    /**
     * Header checks against the previous blocks: the last checkpoint (null when
     * checkpoints are off) and timestamps. Not invoked by ConnectBlock(), and
     * skipped by -reindex-chainstate.
     */
    bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, const CBlockIndex* pindexPrev, int64_t nAdjustedTime, const CBlockIndex* lastCheckpoint) const;
    /**
     * Block checks against the previous block: the coinbase height and that every
     * transaction is final. Not invoked by ConnectBlock(), and skipped by -reindex-chainstate.
     */
    bool ContextualCheckBlock(const CBlock& block, CValidationState& state, const CBlockIndex* pindexPrev) const;

    /**
     * Check whether all inputs of this transaction are valid (no double spends and amounts)
     * This does not modify the UTXO set. This does not check scripts and sigs.
     * @param[out] txfee Set to the transaction fee if successful.
     * Preconditions: tx.IsCoinBase() is false.
     */
    bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee) const;

    /**
     * Calculates the block height and previous block's median time past at
     * which the transaction will be considered final in the context of BIP 68.
     * Also removes from the vector of input heights any entries which did not
     * correspond to sequence locked inputs as they do not affect the calculation.
     */
    std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block) const;
    bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair) const;
    /**
     * Check if transaction is final per BIP 68 sequence numbers and can be included in a block.
     * Consensus critical. Takes as input a list of heights at which tx's inputs (in order) confirmed.
     */
    bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block) const;

    /**
     * Count ECDSA signature operations in pay-to-script-hash inputs.
     * @return maximum number of sigops required to validate this transaction's inputs
     */
    unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs) const;
    /** Total signature operation cost of a transaction, legacy and P2SH. */
    uint32_t GetTransactionSigOps(const CTransaction& tx, const CCoinsViewCache& inputs, int flags) const;

private:
    HeightDerivedConsensusRules Rules(std::optional<int32_t> height, const CXFieldHistoryMap* history) const;

    const CXFieldHistoryMap& m_history;
    std::unique_ptr<const ConsensusSettings> m_settings;
};

#endif // TAPYRUS_CONSENSUSINTERFACE_H
