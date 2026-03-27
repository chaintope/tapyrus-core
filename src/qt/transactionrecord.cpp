// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionrecord.h>

#include <consensus/consensus.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <validation.h>

#include <stdint.h>


/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction()
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}

static std::string tokenTypeName(TokenTypes type)
{
    switch (type) {
        case TokenTypes::REISSUABLE:     return "REISSUABLE";
        case TokenTypes::NON_REISSUABLE: return "NON_REISSUABLE";
        case TokenTypes::NFT:            return "NFT";
        default:                         return "";
    }
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const interfaces::WalletTx& wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.time;

    CAmount nCredit = wtx.getCredit();
    CAmount nDebit = wtx.getDebit();
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.tx->GetHashMalFix();
    std::map<std::string, std::string> mapValue = wtx.value_map;

    // Compute input/output mine status upfront — used by both TPC and token sections
    bool involvesWatchAddress = false;
    isminetype fAllFromMe = ISMINE_SPENDABLE;
    for (isminetype mine : wtx.txin_is_mine) {
        if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
        if (fAllFromMe > mine) fAllFromMe = mine;
    }
    isminetype fAllToMe = ISMINE_SPENDABLE;
    for (isminetype mine : wtx.txout_is_mine) {
        if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
        if (fAllToMe > mine) fAllToMe = mine;
    }


    // Does any output carry a colored script?
    bool hasColoredOutputs = false;
    for (const auto& cid : wtx.txout_color_id) if (!cid.empty()) { hasColoredOutputs = true; break; }

    // Does any input spend a colored UTXO?
    bool hasColoredInputs = false;
    for (unsigned int i = 0; i < wtx.txin_color_id.size(); i++)
        if (!wtx.txin_color_id[i].empty()) { hasColoredInputs = true; break; }

    // If any tokens are involved, suppress standalone TPC records —
    // the token record(s) will carry the full picture (tpcFee in tooltip).
    bool hasTokenInvolvement = hasColoredOutputs || hasColoredInputs;

    if (nNet > 0 || wtx.is_coinbase)
    {
        //
        // Credit
        //
        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];
            isminetype mine = wtx.txout_is_mine[i];
            // Skip token outputs here; they are handled in the token section below
            if (!wtx.txout_color_id[i].empty())
                continue;
            if(mine)
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (wtx.txout_address_is_mine[i])
                {
                    // Received by Bitcoin Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.address = EncodeDestination(wtx.txout_address[i]);
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.address = mapValue["from"];
                }
                if (wtx.is_coinbase)
                {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                parts.append(sub);
            }
        }
    }
    else
    {
        if (fAllFromMe && fAllToMe)
        {
            // Only emit a TPC self-payment record if no tokens are involved.
            // Token transactions (issuance, burn) emit their own token record.
            if (!hasTokenInvolvement)
            {
                CAmount nChange = wtx.getChange();
                parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf, "",
                                -(nDebit - nChange), nCredit - nChange));
                parts.last().involvesWatchAddress = involvesWatchAddress;
            }
        }
        else if (fAllFromMe)
        {
            //
            // Debit (TPC outputs only; token outputs handled below)
            //
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut(ColorIdentifier());

            for (unsigned int nOut = 0; nOut < wtx.tx->vout.size(); nOut++)
            {
                const CTxOut& txout = wtx.tx->vout[nOut];
                // Skip token outputs; they are handled in the token section below
                if (!wtx.txout_color_id[nOut].empty())
                    continue;

                TransactionRecord sub(hash, nTime);
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;

                if(wtx.txout_is_mine[nOut])
                {
                    // Change output going back to our own address — skip it.
                    // It does not represent a meaningful send in the transaction list.
                    continue;
                }

                if (!std::get_if<CNoDestination>(&wtx.txout_address[nOut]))
                {
                    // Sent to Bitcoin Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.address = EncodeDestination(wtx.txout_address[nOut]);
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.address = mapValue["to"];
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0)
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                parts.append(sub);
            }
        }
        else
        {
            // Mixed debit transaction, can't break down payees.
            // Skip if tokens are involved — the token section will emit the record.
            if (!hasTokenInvolvement)
            {
                parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
                parts.last().involvesWatchAddress = involvesWatchAddress;
            }
        }
    }

    // --- Token records ---
    // Compute token credit/debit directly from the transaction's outputs and inputs,
    // similar to how TPC amounts are derived from txout.nValue / prev-out nValue.
    std::map<std::string, CAmount> tokenCredit, tokenDebit;
    for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
        if (wtx.txout_color_id[i].empty()) continue;
        if (wtx.txout_is_mine[i] != ISMINE_NO)
            tokenCredit[wtx.txout_color_id[i]] += wtx.tx->vout[i].nValue;
    }
    for (unsigned int i = 0; i < wtx.txin_color_id.size(); i++) {
        if (wtx.txin_color_id[i].empty()) continue;
        tokenDebit[wtx.txin_color_id[i]] += wtx.txin_amount[i];
    }

    // Collect all distinct color hex strings involved (credit or debit)
    std::set<std::string> allColorHexes;
    for (const auto& e : tokenCredit) allColorHexes.insert(e.first);
    for (const auto& e : tokenDebit)  allColorHexes.insert(e.first);

    std::set<std::string> processedColors;

    // Helper: find the best output index/address for a color with a mine-preference flag.
    auto findColoredOutput = [&](const std::string& colorHex, bool preferMine, int skipIdx = -1)
        -> std::tuple<int, std::string, bool>
    {
        int outIdx = -1;
        std::string addr;
        bool involvesWatch = false;
        for (unsigned int i = 0; i < wtx.txout_color_id.size(); i++) {
            if (wtx.txout_color_id[i] != colorHex) continue;
            if ((int)i == skipIdx) continue;
            bool isMine = wtx.txout_is_mine[i] != ISMINE_NO;
            if (preferMine && !isMine) continue;
            if (!preferMine && isMine) continue;
            outIdx = i;
            if (!std::get_if<CNoDestination>(&wtx.txout_address[i]))
                addr = EncodeDestination(wtx.txout_address[i]);
            involvesWatch = wtx.txout_is_mine[i] & ISMINE_WATCH_ONLY;
            break;
        }
        // Fallback: take any remaining output with this color
        if (outIdx < 0) {
            for (unsigned int i = 0; i < wtx.txout_color_id.size(); i++) {
                if (wtx.txout_color_id[i] != colorHex) continue;
                if ((int)i == skipIdx) continue;
                outIdx = i;
                if (!std::get_if<CNoDestination>(&wtx.txout_address[i]))
                    addr = EncodeDestination(wtx.txout_address[i]);
                involvesWatch = wtx.txout_is_mine[i] & ISMINE_WATCH_ONLY;
                break;
            }
        }
        return {outIdx, addr, involvesWatch};
    };

    auto appendTokenRecord = [&](const ColorIdentifier& colorId, CAmount creditAmt, CAmount debitAmt) {
        std::string colorHex = colorId.toHexString();
        if (!processedColors.insert(colorHex).second)
            return; // already emitted a record for this color

        CAmount netAmt = creditAmt - debitAmt;

        bool hasColoredOutputForColor = false;
        for (const auto& cid : wtx.txout_color_id)
            if (cid == colorHex) { hasColoredOutputForColor = true; break; }

        bool isIssue = (netAmt > 0 && fAllFromMe);
        bool isBurn  = (netAmt < 0 && !hasColoredOutputForColor);

        // For transfers with both credit and debit: show two entries so the
        // user can differentiate the send side (negative) from the receive
        // side (positive) by address.
        if (!isIssue && !isBurn && creditAmt > 0 && debitAmt > 0) {
            auto [creditIdx, creditAddr, creditWatch] = findColoredOutput(colorHex, /*preferMine=*/true);
            auto [debitIdx,  debitAddr,  debitWatch]  = findColoredOutput(colorHex, /*preferMine=*/false, creditIdx);

            TransactionRecord credit(hash, nTime);
            credit.idx    = creditIdx >= 0 ? creditIdx : 0;
            credit.colorId    = colorHex;
            credit.tokenType  = tokenTypeName(colorId.type);
            credit.tokenAmount = creditAmt;
            credit.involvesWatchAddress = creditWatch;
            credit.address = creditAddr;
            credit.type   = TransactionRecord::TokenTransfer;
            parts.append(credit);

            TransactionRecord debit(hash, nTime);
            debit.idx    = debitIdx >= 0 ? debitIdx : credit.idx;
            debit.colorId    = colorHex;
            debit.tokenType  = tokenTypeName(colorId.type);
            debit.tokenAmount = -debitAmt;
            debit.involvesWatchAddress = debitWatch;
            debit.address = debitAddr;
            debit.type   = TransactionRecord::TokenTransfer;
            parts.append(debit);
            return;
        }

        // Single record for issuance, burn, or pure send/receive
        auto [outIdx, addr, involvesWatch] = findColoredOutput(colorHex, netAmt >= 0);

        TransactionRecord sub(hash, nTime);
        sub.idx = outIdx >= 0 ? outIdx : 0;
        sub.colorId = colorHex;
        sub.tokenType = tokenTypeName(colorId.type);
        sub.tokenAmount = netAmt;
        sub.involvesWatchAddress = involvesWatch;
        sub.address = addr;

        if (isIssue) {
            sub.type = TransactionRecord::TokenIssue;
        } else if (isBurn) {
            sub.type = TransactionRecord::TokenBurn;
        } else {
            sub.type = TransactionRecord::TokenTransfer;
        }

        parts.append(sub);
    };

    for (const std::string& colorHex : allColorHexes) {
        const std::vector<unsigned char> vColor = ParseHex(colorHex);
        ColorIdentifier colorId(vColor);
        CAmount cAmt = tokenCredit.count(colorHex) ? tokenCredit[colorHex] : 0;
        CAmount dAmt = tokenDebit.count(colorHex)  ? tokenDebit[colorHex]  : 0;
        appendTokenRecord(colorId, cAmt, dAmt);
    }

    // Compute fee: only meaningful when wallet funded all inputs
    CAmount tpcFeeAmt = 0;
    if (fAllFromMe != ISMINE_NO)
        tpcFeeAmt = nDebit - wtx.tx->GetValueOut(ColorIdentifier());
    if (tpcFeeAmt < 0) tpcFeeAmt = 0; // guard against rounding / mixed-input edge cases

    // Stamp fee on every record so the tooltip can display it
    for (TransactionRecord& sub : parts)
        sub.tpcFee = tpcFeeAmt;

    return parts;
}

void TransactionRecord::updateStatus(const interfaces::WalletTxStatus& wtx, int numBlocks, int64_t adjustedTime)
{
    // Determine transaction status

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        wtx.block_height,
        wtx.is_coinbase ? 1 : 0,
        wtx.time_received,
        idx);
    status.countsForBalance = wtx.is_trusted;
    status.depth = wtx.depth_in_main_chain;
    status.cur_num_blocks = numBlocks;

    if (!wtx.is_final)
    {
        if (wtx.lock_time < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.lock_time - numBlocks;
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.lock_time;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.is_in_main_chain)
        {
            status.status = TransactionStatus::Confirmed;
        }
        else
        {
            status.status = TransactionStatus::NotAccepted;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.is_abandoned)
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded(int numBlocks) const
{
    return status.cur_num_blocks != numBlocks || status.needsUpdate;
}

QString TransactionRecord::getTxHash() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
