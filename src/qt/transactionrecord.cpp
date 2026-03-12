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

    // Does this tx involve any token credits or debits?
    bool hasTokenCredits = false;
    bool hasTokenDebits  = false;
    for (const auto& e : wtx.credits) if (e.first.type != TokenTypes::NONE) { hasTokenCredits = true; break; }
    for (const auto& e : wtx.debits)  if (e.first.type != TokenTypes::NONE) { hasTokenDebits  = true; break; }

    // Does any output carry a colored script?
    bool hasColoredOutputs = false;
    for (const auto& cid : wtx.txout_color_id) if (!cid.empty()) { hasColoredOutputs = true; break; }

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
            // Classify self-payment: anchor tx, token fee, or regular payment to self
            TransactionRecord::Type selfType;
            if (hasColoredOutputs && !hasTokenCredits && !hasTokenDebits)
                selfType = TransactionRecord::TokenAnchor;  // tx1: creates colored UTXO
            else if (hasTokenCredits || hasTokenDebits)
                selfType = TransactionRecord::TokenFee;     // tx2: anchor consumed by issuance
            else
                selfType = TransactionRecord::SendToSelf;

            CAmount nChange = wtx.getChange();
            parts.append(TransactionRecord(hash, nTime, selfType, "",
                            -(nDebit - nChange), nCredit - nChange));
            parts.last().involvesWatchAddress = involvesWatchAddress;
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
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
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
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
            parts.last().involvesWatchAddress = involvesWatchAddress;
        }
    }

    // --- Token records ---
    // Emit one record per distinct colored coin type involved in this transaction.
    // Token amounts come from the credits/debits maps; txout.nValue is always 0 for colored outputs.
    std::set<std::string> processedColors;

    auto appendTokenRecord = [&](const ColorIdentifier& colorId, CAmount creditAmt, CAmount debitAmt) {
        std::string colorHex = colorId.toHexString();
        if (!processedColors.insert(colorHex).second)
            return; // already emitted a record for this color

        CAmount netAmt = creditAmt - debitAmt;

        // Find the best output index and address for this color
        std::string addr;
        int outIdx = -1;
        bool involvesWatch = false;

        for (unsigned int i = 0; i < wtx.txout_color_id.size(); i++) {
            if (wtx.txout_color_id[i] != colorHex)
                continue;
            bool isMine = wtx.txout_is_mine[i] != ISMINE_NO;
            if (netAmt > 0 && !isMine) continue; // for receives, prefer mine outputs
            if (netAmt < 0 &&  isMine) continue; // for sends, prefer non-mine outputs
            outIdx = i;
            if (!std::get_if<CNoDestination>(&wtx.txout_address[i]))
                addr = EncodeDestination(wtx.txout_address[i]);
            involvesWatch = wtx.txout_is_mine[i] & ISMINE_WATCH_ONLY;
            break;
        }
        // Fallback: if no preferred output found, take any output with this color
        if (outIdx < 0) {
            for (unsigned int i = 0; i < wtx.txout_color_id.size(); i++) {
                if (wtx.txout_color_id[i] != colorHex) continue;
                outIdx = i;
                if (!std::get_if<CNoDestination>(&wtx.txout_address[i]))
                    addr = EncodeDestination(wtx.txout_address[i]);
                break;
            }
        }

        TransactionRecord sub(hash, nTime);
        sub.idx = outIdx >= 0 ? outIdx : 0;
        sub.colorId = colorHex;
        sub.tokenType = tokenTypeName(colorId.type);
        sub.tokenAmount = netAmt;
        sub.involvesWatchAddress = involvesWatch;
        sub.address = addr;

        if (netAmt > 0) {
            // Distinguish fresh/reissuance (all TPC inputs mine, I'm the issuer)
            // from a transfer receive (sender's inputs, not mine)
            if (fAllFromMe)
                sub.type = TransactionRecord::TokenIssued;
            else
                sub.type = addr.empty() ? TransactionRecord::RecvFromOther : TransactionRecord::RecvWithAddress;
        } else if (netAmt < 0) {
            // Burn: no colored output with this color exists in the tx (tokens destroyed)
            // Send: colored output exists (going to recipient)
            bool hasColoredOutputForColor = false;
            for (const auto& cid : wtx.txout_color_id)
                if (cid == colorHex) { hasColoredOutputForColor = true; break; }
            if (!hasColoredOutputForColor)
                sub.type = TransactionRecord::TokenBurned;
            else
                sub.type = addr.empty() ? TransactionRecord::SendToOther : TransactionRecord::SendToAddress;
        } else {
            sub.type = TransactionRecord::SendToSelf;
        }

        parts.append(sub);
    };

    for (const auto& entry : wtx.credits) {
        if (entry.first.type == TokenTypes::NONE) continue;
        appendTokenRecord(entry.first, entry.second, wtx.getDebit(entry.first));
    }
    for (const auto& entry : wtx.debits) {
        if (entry.first.type == TokenTypes::NONE) continue;
        appendTokenRecord(entry.first, wtx.getCredit(entry.first), entry.second);
    }

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
