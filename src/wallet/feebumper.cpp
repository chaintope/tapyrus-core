// Copyright (c) 2017-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coloridentifier.h>
#include <consensus/validation.h>
#include <wallet/coincontrol.h>
#include <wallet/feebumper.h>
#include <wallet/fees.h>
#include <wallet/wallet.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <validation.h> //for mempool access
#include <txmempool.h>
#include <utilmoneystr.h>
#include <util.h>
#include <net.h>

//! Check whether transaction has descendant in wallet or mempool, or has been
//! mined, or conflicts with a mined transaction. Return a feebumper::Result.
static feebumper::Result PreconditionChecks(const CWallet* wallet, const CWalletTx& wtx, std::vector<std::string>& errors) EXCLUSIVE_LOCKS_REQUIRED(wallet->cs_wallet)
{
    if (wallet->HasWalletSpend(wtx.GetHash())) {
        errors.push_back("Transaction has descendants in the wallet");
        return feebumper::Result::INVALID_PARAMETER;
    }

    {
        LOCK(mempool.cs);
        auto it_mp = mempool.mapTx.find(wtx.GetHash());
        if (it_mp != mempool.mapTx.end() && it_mp->GetCountWithDescendants() > 1) {
            errors.push_back("Transaction has descendants in the mempool");
            return feebumper::Result::INVALID_PARAMETER;
        }
    }

    if (wtx.GetDepthInMainChain() != 0) {
        errors.push_back("Transaction has been mined, or is conflicted with a mined transaction");
        return feebumper::Result::WALLET_ERROR;
    }

    if (!SignalsOptInRBF(*wtx.tx)) {
        errors.push_back("Transaction is not BIP 125 replaceable");
        return feebumper::Result::WALLET_ERROR;
    }

    if (wtx.mapValue.count("replaced_by_txid")) {
        errors.push_back(strprintf("Cannot bump transaction %s which was already bumped by transaction %s", wtx.GetHash().ToString(), wtx.mapValue.at("replaced_by_txid")));
        return feebumper::Result::WALLET_ERROR;
    }

    // check that original tx consists entirely of our inputs
    // if not, we can't bump the fee, because the wallet has no way of knowing the value of the other inputs (thus the fee)
    if (!wallet->IsAllFromMe(*wtx.tx, ISMINE_SPENDABLE)) {
        errors.push_back("Transaction contains inputs that don't belong to this wallet");
        return feebumper::Result::WALLET_ERROR;
    }


    return feebumper::Result::OK;
}

namespace feebumper {

bool TransactionCanBeBumped(const CWallet* wallet, const uint256& txid)
{
    LOCK2(cs_main, wallet->cs_wallet);
    const CWalletTx* wtx = wallet->GetWalletTx(txid);
    if (wtx == nullptr) return false;

    std::vector<std::string> errors_dummy;
    feebumper::Result res = PreconditionChecks(wallet, *wtx, errors_dummy);
    return res == feebumper::Result::OK;
}

Result CreateTransaction(const CWallet* wallet, const uint256& txid, const CCoinControl& coin_control, CAmount total_fee, std::vector<std::string>& errors,
                         CAmount& old_fee, CAmount& new_fee, CMutableTransaction& mtx)
{
    LOCK2(cs_main, wallet->cs_wallet);
    errors.clear();
    auto it = wallet->mapWallet.find(txid);
    if (it == wallet->mapWallet.end()) {
        errors.push_back("Invalid or non-wallet transaction id");
        return Result::INVALID_ADDRESS_OR_KEY;
    }
    const CWalletTx& wtx = it->second;

    Result result = PreconditionChecks(wallet, wtx, errors);
    if (result != Result::OK) {
        return result;
    }

    // figure out which output was change
    // if there was no change output or multiple change outputs, fail
    // Colored outputs are skipped: their nValue is a token amount, not tapyrus,
    // so subtracting a TPC fee delta from them silently destroys tokens.
    int nOutput = -1;
    for (size_t i = 0; i < wtx.tx->vout.size(); ++i) {
        if (GetColorIdFromScript(wtx.tx->vout[i].scriptPubKey).type != TokenTypes::NONE) {
            continue;  // never adjust a colored output to cover a fee delta
        }
        if (wallet->IsChange(wtx.tx->vout[i])) {
            if (nOutput != -1) {
                errors.push_back("Transaction has multiple change outputs");
                return Result::WALLET_ERROR;
            }
            nOutput = i;
        }
    }
    // For colored transfers the original tx may have no TPC change output (e.g. when
    // BnB covered the fee exactly).  When fAllowOtherInputs is set the caller has
    // pre-arranged a destChange address; find a fresh TPC UTXO to add as a new input.
    COutPoint newInputOp;
    CAmount   newInputValue = 0;
    if (nOutput == -1) {
        if (!coin_control.fAllowOtherInputs || !IsValidDestination(coin_control.destChange)) {
            errors.push_back("Cannot bump fee: transaction has no TPC change output to absorb the fee delta");
            return Result::WALLET_ERROR;
        }
        std::vector<COutput> vAvailableCoins;
        // Require at least 1 confirmation: adding a 0-conf input to a fee-bump
        // creates an unconfirmed chain and defeats the purpose of RBF.
        wallet->AvailableCoins(vAvailableCoins, true, &coin_control,
                               1, MAX_MONEY, MAX_MONEY, 0, /*nMinDepth=*/1);
        for (const auto& out : vAvailableCoins) {
            if (!out.fSpendable) continue;
            const CTxOut& txout = out.tx->tx->vout[out.i];
            if (GetColorIdFromScript(txout.scriptPubKey).type != TokenTypes::NONE) continue;
            COutPoint op(out.tx->tx->GetHashMalFix(), out.i);
            bool alreadyIn = false;
            for (const auto& vin : wtx.tx->vin)
                if (vin.prevout == op) { alreadyIn = true; break; }
            if (alreadyIn) continue;
            // Keep the largest TPC UTXO to maximise the chance of covering the fee delta.
            if (txout.nValue > newInputValue) {
                newInputOp    = op;
                newInputValue = txout.nValue;
            }
        }
        if (newInputOp.IsNull()) {
            errors.push_back("Cannot bump fee: no available TPC UTXO to add as new input");
            return Result::WALLET_ERROR;
        }
    }

    // Build the candidate transaction first so that CalculateMaximumSignedTxSize accounts for
    // any new input and placeholder change output when there is no TPC change output.
    bool rbf = coin_control.m_signal_bip125_rbf.get_value_or(wallet->m_signal_rbf);
    mtx = CMutableTransaction{*wtx.tx};
    if (nOutput == -1) {
        mtx.vin.push_back(CTxIn(newInputOp, CScript(), rbf ? MAX_BIP125_RBF_SEQUENCE : 0xfffffffe));
        // Placeholder change output with full input value; corrected after fee delta is computed.
        mtx.vout.push_back(CTxOut(newInputValue, GetScriptForDestination(coin_control.destChange)));
    }

    // Calculate the expected size of the new transaction.
    int64_t txSize = GetTransactionSize(*(wtx.tx));
    const int64_t maxNewTxSize = CalculateMaximumSignedTxSize(CTransaction(mtx), wallet);
    if (maxNewTxSize < 0) {
        errors.push_back("Transaction contains inputs that cannot be signed");
        return Result::INVALID_ADDRESS_OR_KEY;
    }

    // calculate the old fee and fee-rate
    ColorIdentifier colorId;
    old_fee = wtx.GetDebit(ISMINE_SPENDABLE, colorId) - wtx.tx->GetValueOut(colorId);
    CFeeRate nOldFeeRate(old_fee, txSize);
    CFeeRate nNewFeeRate;
    // The wallet uses a conservative WALLET_INCREMENTAL_RELAY_FEE value to
    // future proof against changes to network wide policy for incremental relay
    // fee that our node may not be aware of.
    CFeeRate walletIncrementalRelayFee = CFeeRate(WALLET_INCREMENTAL_RELAY_FEE);
    if (::incrementalRelayFee > walletIncrementalRelayFee) {
        walletIncrementalRelayFee = ::incrementalRelayFee;
    }

    if (total_fee > 0) {
        CAmount minTotalFee = nOldFeeRate.GetFee(maxNewTxSize) + ::incrementalRelayFee.GetFee(maxNewTxSize);
        if (total_fee < minTotalFee) {
            errors.push_back(strprintf("Insufficient totalFee, must be at least %s (oldFee %s + incrementalFee %s)",
                                                                FormatMoney(minTotalFee), FormatMoney(nOldFeeRate.GetFee(maxNewTxSize)), FormatMoney(::incrementalRelayFee.GetFee(maxNewTxSize))));
            return Result::INVALID_PARAMETER;
        }
        CAmount requiredFee = GetRequiredFee(*wallet, maxNewTxSize);
        if (total_fee < requiredFee) {
            errors.push_back(strprintf("Insufficient totalFee (cannot be less than required fee %s)",
                                                                FormatMoney(requiredFee)));
            return Result::INVALID_PARAMETER;
        }
        new_fee = total_fee;
        nNewFeeRate = CFeeRate(total_fee, maxNewTxSize);
    } else {
        new_fee = GetMinimumFee(*wallet, maxNewTxSize, coin_control, mempool, ::feeEstimator, nullptr /* FeeCalculation */);
        nNewFeeRate = CFeeRate(new_fee, maxNewTxSize);

        // New fee rate must be at least old rate + minimum incremental relay rate
        // walletIncrementalRelayFee.GetFeePerK() should be exact, because it's initialized
        // in that unit (fee per kb).
        // However, nOldFeeRate is a calculated value from the tx fee/size, so
        // add 1 tapyrus to the result, because it may have been rounded down.
        if (nNewFeeRate.GetFeePerK() < nOldFeeRate.GetFeePerK() + 1 + walletIncrementalRelayFee.GetFeePerK()) {
            nNewFeeRate = CFeeRate(nOldFeeRate.GetFeePerK() + 1 + walletIncrementalRelayFee.GetFeePerK());
            new_fee = nNewFeeRate.GetFee(maxNewTxSize);
        }
    }

    // Check that in all cases the new fee doesn't violate maxTxFee
     if (new_fee > maxTxFee) {
         errors.push_back(strprintf("Specified or calculated fee %s is too high (cannot be higher than maxTxFee %s)",
                               FormatMoney(new_fee), FormatMoney(maxTxFee)));
         return Result::WALLET_ERROR;
     }

    // check that fee rate is higher than mempool's minimum fee
    // (no point in bumping fee if we know that the new tx won't be accepted to the mempool)
    // This may occur if the user set TotalFee or paytxfee too low, if fallbackfee is too low, or, perhaps,
    // in a rare situation where the mempool minimum fee increased significantly since the fee estimation just a
    // moment earlier. In this case, we report an error to the user, who may use total_fee to make an adjustment.
    CFeeRate minMempoolFeeRate = mempool.GetMinFee(gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000);
    if (nNewFeeRate.GetFeePerK() < minMempoolFeeRate.GetFeePerK()) {
        errors.push_back(strprintf(
            "New fee rate (%s) is lower than the minimum fee rate (%s) to get into the mempool -- "
            "the totalFee value should be at least %s or the settxfee value should be at least %s to add transaction",
            FormatMoney(nNewFeeRate.GetFeePerK()),
            FormatMoney(minMempoolFeeRate.GetFeePerK()),
            FormatMoney(minMempoolFeeRate.GetFee(maxNewTxSize)),
            FormatMoney(minMempoolFeeRate.GetFeePerK())));
        return Result::WALLET_ERROR;
    }

    CAmount nDelta = new_fee - old_fee;
    assert(nDelta > 0);

    if (nOutput != -1) {
        // Reduce the existing TPC change output to cover the fee delta.
        CTxOut* poutput = &(mtx.vout[nOutput]);
        if (poutput->nValue < nDelta) {
            errors.push_back("Change output is too small to bump the fee");
            return Result::WALLET_ERROR;
        }
        poutput->nValue -= nDelta;
        if (poutput->nValue <= GetDustThreshold(*poutput, GetDiscardRate(*wallet, ::feeEstimator))) {
            wallet->WalletLogPrintf("Bumping fee and discarding dust output\n");
            new_fee += poutput->nValue;
            mtx.vout.erase(mtx.vout.begin() + nOutput);
        }
    } else {
        // Correct the placeholder change output (last output) with the actual surplus.
        if (newInputValue < nDelta) {
            errors.push_back("Cannot bump fee: new TPC input insufficient to cover fee delta");
            return Result::WALLET_ERROR;
        }
        CAmount changeValue = newInputValue - nDelta;
        CTxOut& changeOut = mtx.vout.back();
        changeOut.nValue = changeValue;
        if (changeValue <= GetDustThreshold(changeOut, GetDiscardRate(*wallet, ::feeEstimator))) {
            wallet->WalletLogPrintf("Bumping fee: discarding dust change from new TPC input\n");
            new_fee += changeValue;
            mtx.vout.pop_back();
        }
    }

    // Mark new tx not replaceable, if requested.
    if (!rbf) {
        for (auto& input : mtx.vin) {
            if (input.nSequence < 0xfffffffe) input.nSequence = 0xfffffffe;
        }
    }

    return Result::OK;
}

bool SignTransaction(CWallet* wallet, CMutableTransaction& mtx) {
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->SignTransaction(mtx);
}

Result CommitTransaction(CWallet* wallet, const uint256& txid, CMutableTransaction&& mtx, std::vector<std::string>& errors, uint256& bumped_txid)
{
    LOCK2(cs_main, wallet->cs_wallet);
    if (!errors.empty()) {
        return Result::MISC_ERROR;
    }
    auto it = txid.IsNull() ? wallet->mapWallet.end() : wallet->mapWallet.find(txid);
    if (it == wallet->mapWallet.end()) {
        errors.push_back("Invalid or non-wallet transaction id");
        return Result::MISC_ERROR;
    }
    CWalletTx& oldWtx = it->second;

    // make sure the transaction still has no descendants and hasn't been mined in the meantime
    Result result = PreconditionChecks(wallet, oldWtx, errors);
    if (result != Result::OK) {
        return result;
    }

    // commit/broadcast the tx
    CTransactionRef tx = MakeTransactionRef(std::move(mtx));
    mapValue_t mapValue = oldWtx.mapValue;
    mapValue["replaces_txid"] = oldWtx.GetHash().ToString();

    CReserveKey reservekey(wallet);
    CValidationState state;
    if (!wallet->CommitTransaction(tx, std::move(mapValue), oldWtx.vOrderForm, reservekey, g_connman.get(), state)) {
        // NOTE: CommitTransaction never returns false, so this should never happen.
        errors.push_back(strprintf("The transaction was rejected: %s", FormatStateMessage(state)));
        return Result::WALLET_ERROR;
    }

    bumped_txid = tx->GetHashMalFix();
    if (state.IsInvalid()) {
        // This can happen if the mempool rejected the transaction.  Report
        // what happened in the "errors" response.
        errors.push_back(strprintf("Error: The transaction was rejected: %s", FormatStateMessage(state)));
    }

    // mark the original tx as bumped
    if (!wallet->MarkReplaced(oldWtx.GetHash(), bumped_txid)) {
        // TODO: see if JSON-RPC has a standard way of returning a response
        // along with an exception. It would be good to return information about
        // wtxBumped to the caller even if marking the original transaction
        // replaced does not succeed for some reason.
        errors.push_back("Created new bumpfee transaction but could not mark the original transaction as replaced");
    }
    return Result::OK;
}

} // namespace feebumper
