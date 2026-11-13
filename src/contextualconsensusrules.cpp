// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <contextualconsensusrules.h>

#include <blockindex.h>
#include <coinsviewcache.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <util.h>
#include <utilmoneystr.h>
#include <script/interpreter.h>

#include <assert.h>

bool ContextualConsensusRules::ContextualCheckBlockHeader(const CBlockHeader& header, CValidationState& state, const CBlockIndex& prev, int64_t nAdjustedTime, const CBlockIndex* lastCheckpoint)
{
    const int nHeight = prev.nHeight + 1;

    // Don't accept any forks from the prod chain prior to the last checkpoint.
    if (lastCheckpoint && nHeight < lastCheckpoint->nHeight)
        return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight), REJECT_CHECKPOINT, "bad-fork-prior-to-checkpoint");

    // Check timestamp against prev
    if (header.GetBlockTime() <= prev.GetMedianTimePast())
        return state.Invalid(false, REJECT_INVALID, "time-too-old", "block's timestamp is too early");

    // Check timestamp
    if (header.GetBlockTime() > nAdjustedTime + ConsensusConstants::MAX_FUTURE_BLOCK_TIME)
        return state.Invalid(false, REJECT_INVALID, "time-too-new", "block timestamp too far in the future");

    return true;
}

bool ContextualConsensusRules::CheckCoinbaseHeight(const CBlock& block, CValidationState& state, const CBlockIndex& prev)
{
    const int nHeight = prev.nHeight + 1;

    // A Tapyrus coinbase records its block's height in prevout.n.
    if (block.GetHeight() != static_cast<uint32_t>(nHeight))
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-invalid", false, "incorrect block height in coinbase");

    return true;
}

bool ContextualConsensusRules::ContextualCheckBlock(const CBlock& block, CValidationState& state, const CBlockIndex& prev)
{
    const int nHeight = prev.nHeight + 1;

    if (!CheckCoinbaseHeight(block, state, prev))
        return false;

    int64_t nLockTimeCutoff = prev.GetMedianTimePast();

    // Check that all transactions are finalized
    for (const auto& tx : block.vtx) {
        if (!IsFinalTx(*tx, nHeight, nLockTimeCutoff)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal", false, "non-final transaction");
        }
    }

    return true;
}

bool ContextualConsensusRules::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
                         strprintf("%s: inputs missing/spent", __func__));
    }

    CAmount nValueIn = 0;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight <= coin.nHeight) {
            return state.Invalid(false,
                REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        // Check for negative or overflow input values
        ColorIdentifier cid = GetColorIdFromScript(coin.out.scriptPubKey);
        if(cid.type == TokenTypes::NONE)
            nValueIn += coin.out.nValue;
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValueIn)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        }
    }

    const CAmount value_out = tx.GetValueOut(ColorIdentifier());
    if (nValueIn < value_out) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
            strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(value_out)));
    }

    // Tally transaction fees
    const CAmount txfee_aux = nValueIn - value_out;
    if (!MoneyRange(txfee_aux)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    }

    txfee = txfee_aux;
    return true;
}

std::pair<int, int64_t> ContextualConsensusRules::CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    bool fEnforceBIP68 = flags & ConsensusConstants::LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & ConsensusConstants::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & ConsensusConstants::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & ConsensusConstants::SEQUENCE_LOCKTIME_MASK) << ConsensusConstants::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & ConsensusConstants::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool ContextualConsensusRules::EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool ContextualConsensusRules::SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int ContextualConsensusRules::GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        // CP2SH sigop counting is not gated by SCRIPT_VERIFY_CP2SH: mainnet activates
        // CP2SH at genesis, testnet activation height 693367 is behind the current tip,
        // and testnet sync confirmed no historical block exceeds the sigop limit.
        // v0.7.1↔v0.7.2 split risk is testnet-only where the federation is trusted.
        if (prevout.scriptPubKey.IsPayToScriptHash() || prevout.scriptPubKey.IsColoredPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

uint32_t ContextualConsensusRules::GetTransactionSigOps(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    int32_t nSigOps = GetLegacySigOpCount(tx);

    if (tx.IsCoinBase())
        return nSigOps;

    //in Tapyrus we always verify P2SH
    nSigOps += GetP2SHSigOpCount(tx, inputs);

    return nSigOps;
}
