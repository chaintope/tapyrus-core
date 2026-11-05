// Copyright (c) 2017-2017 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>

#include <consensus/tx_check.h>

#include <consensusconstants.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <consensus/validation.h>
#include <tinyformat.h>

// TODO remove the following dependencies
#include <coin.h>
#include <coinsviewcache.h>
#include <utilmoneystr.h>

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < ConsensusConstants::LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == ConsensusConstants::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

// CalculateSequenceLocks()/EvaluateSequenceLocks()/SequenceLocks() moved to
// consensus/sequence_locks.cpp -- see the file-level comment there for why.

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
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

uint32_t GetTransactionSigOps(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    int32_t nSigOps = GetLegacySigOpCount(tx);

    if (tx.IsCoinBase())
        return nSigOps;

    //in Tapyrus we always verify P2SH
    nSigOps += GetP2SHSigOpCount(tx, inputs);

    return nSigOps;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee)
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
