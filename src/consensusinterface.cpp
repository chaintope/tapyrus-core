// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensusinterface.h>

#include <blockindex.h>
#include <coinsviewcache.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <util.h>
#include <utilmoneystr.h>
#include <primitives/xfieldvalues.h>
#include <script/interpreter.h>
#include <xfieldhistorymap.h>

#include <assert.h>
#include <limits>

const ConsensusSettings& ConsensusInterface::Settings() const
{
    assert(m_settings);
    return *m_settings;
}

HeightDerivedConsensusRules ConsensusInterface::RulesAt(const CBlockIndex& block, const CXFieldHistoryMap* history) const
{
    return Rules(block.nHeight, history);
}

HeightDerivedConsensusRules ConsensusInterface::RulesAfter(const CBlockIndex& prev, const CXFieldHistoryMap* history) const
{
    return Rules(prev.nHeight + 1, history);
}

HeightDerivedConsensusRules ConsensusInterface::RulesLatest(const CXFieldHistoryMap* history) const
{
    return Rules(std::nullopt, history);
}

HeightDerivedConsensusRules ConsensusInterface::Rules(std::optional<int32_t> height, const CXFieldHistoryMap* history) const
{
    const CXFieldHistoryMap& xfieldHistory = history ? *history : m_history;
    // An unknown height reads the latest entries.
    const uint32_t lookup = height ? static_cast<uint32_t>(*height) : std::numeric_limits<uint32_t>::max();
    const XFieldAggPubKey aggregatePubKey = std::get<XFieldAggPubKey>(xfieldHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, lookup).xfieldValue);
    const XFieldMaxBlockSize maxBlockSize = std::get<XFieldMaxBlockSize>(xfieldHistory.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, lookup).xfieldValue);
    // A soft fork with no registration on this network is active from genesis.
    const int32_t flagsHeight = height ? *height : std::numeric_limits<int32_t>::max();
    uint32_t scriptFlags = SCRIPT_VERIFY_NONE;
    if (IsActive(SCRIPT_VERIFY_CP2SH_COLORED, flagsHeight))
        scriptFlags |= SCRIPT_VERIFY_CP2SH_COLORED;
    return HeightDerivedConsensusRules(height, maxBlockSize.data, aggregatePubKey.getPubKey(), scriptFlags);
}

bool ConsensusInterface::IsActive(uint32_t flag, int32_t height) const
{
    return Settings().SoftForkManager().IsActive(Settings().NetworkId(), flag, height);
}

uint32_t ConsensusInterface::ActivationFlagsChange(int32_t oldHeight, int32_t newHeight) const
{
    return Settings().SoftForkManager().ActivationFlagsChange(Settings().NetworkId(), oldHeight, newHeight);
}

bool ConsensusInterface::ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, const CBlockIndex* pindexPrev, int64_t nAdjustedTime, const CBlockIndex* lastCheckpoint) const
{
    assert(pindexPrev != nullptr);
    const int nHeight = pindexPrev->nHeight + 1;

    // Don't accept any forks from the prod chain prior to the last checkpoint.
    if (lastCheckpoint && nHeight < lastCheckpoint->nHeight)
        return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight), REJECT_CHECKPOINT, "bad-fork-prior-to-checkpoint");

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast())
        return state.Invalid(false, REJECT_INVALID, "time-too-old", "block's timestamp is too early");

    // Check timestamp
    if (block.GetBlockTime() > nAdjustedTime + ConsensusConstants::MAX_FUTURE_BLOCK_TIME)
        return state.Invalid(false, REJECT_INVALID, "time-too-new", "block timestamp too far in the future");

    return true;
}

bool ConsensusInterface::ContextualCheckBlock(const CBlock& block, CValidationState& state, const CBlockIndex* pindexPrev) const
{
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;

    // A Tapyrus coinbase records its block's height in prevout.n.
    if (block.GetHeight() != static_cast<uint32_t>(nHeight))
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-invalid", false, "incorrect block height in coinbase");

    int64_t nLockTimeCutoff = pindexPrev->GetMedianTimePast();

    // Check that all transactions are finalized
    for (const auto& tx : block.vtx) {
        if (!IsFinalTx(*tx, nHeight, nLockTimeCutoff)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal", false, "non-final transaction");
        }
    }

    return true;
}

bool ConsensusInterface::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee) const
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

std::pair<int, int64_t> ConsensusInterface::CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block) const
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nFeatures is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nFeatures
    // wouldn't support BIP 68.
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

bool ConsensusInterface::EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair) const
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool ConsensusInterface::SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block) const
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int ConsensusInterface::GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs) const
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

uint32_t ConsensusInterface::GetTransactionSigOps(const CTransaction& tx, const CCoinsViewCache& inputs, int flags) const
{
    int32_t nSigOps = GetLegacySigOpCount(tx);

    if (tx.IsCoinBase())
        return nSigOps;

    //in Tapyrus we always verify P2SH
    nSigOps += GetP2SHSigOpCount(tx, inputs);

    return nSigOps;
}
