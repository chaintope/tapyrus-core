// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionrecord.h>

#include <consensus/consensus.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <timedata.h>
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

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(interfaces::Node& node, const interfaces::WalletTx& wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.time;

    std::set<ColorIdentifier> txColorIdSet{wtx.getAllColorIds(node)};

    //credit, debit and net are separate for each colorid
    std::map<const ColorIdentifier, std::tuple<CAmount, CAmount, CAmount> > creditMap;
    for(auto color : txColorIdSet)
    {
        CAmount nCredit = wtx.getCredit(color);
        CAmount nDebit = wtx.getDebit(color);
        CAmount nNet = nCredit - nDebit;
        creditMap.emplace(color, std::make_tuple(nCredit, nDebit, nNet) );
    }

    uint256 hash = wtx.tx->GetHashMalFix();
    std::map<std::string, std::string> mapValue = wtx.value_map;

    bool involvesWatchAddress = false;
    isminetype fAllFromMe = ISMINE_SPENDABLE;
    for (isminetype mine : wtx.txin_is_mine)
    {
        if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
        if(fAllFromMe > mine) fAllFromMe = mine;
    }

    isminetype fAllToMe = ISMINE_SPENDABLE;
    for (isminetype mine : wtx.txout_is_mine)
    {
        if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
        if(fAllToMe > mine) fAllToMe = mine;
    }

    for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
    {
        const CTxOut& txout = wtx.tx->vout[i];
        ColorIdentifier colorId = GetColorIdFromScript(txout.scriptPubKey);

        CAmount nCredit = std::get<0>(creditMap[colorId]);
        CAmount nDebit = std::get<1>(creditMap[colorId]);
        CAmount nNet = std::get<2>(creditMap[colorId]);

        if (nNet > 0 || wtx.is_coinbase)
        {
            //
            // Credit
            //

            isminetype mine = wtx.txout_is_mine[i];
            if(!mine)
                continue;

            TransactionRecord sub(hash, nTime);
            sub.idx = i; // vout index
            sub.credit = nCredit;
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            if (wtx.txout_address_is_mine[i])
            {
                // Received by Tapyrus Address
                if(colorId.type == TokenTypes::NONE)
                    sub.type = TransactionRecord::RecvWithAddress;
                else
                    sub.type = TransactionRecord::TokenRecvWithAddress;
                sub.address = EncodeDestination(wtx.txout_address[i]);
            }
            else
            {
                // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                if(colorId.type == TokenTypes::NONE)
                    sub.type = TransactionRecord::RecvFromOther;
                else
                    sub.type = TransactionRecord::TokenRecvFromOther;
                sub.address = mapValue["from"];
            }


            if(!wtx.is_tokenInput &&  wtx.is_tokenOutput);
            {
                sub.type = TransactionRecord::TokenIssue;
            }

            if(wtx.is_coinbase)
            {
                sub.type = TransactionRecord::Generated;
            }

            parts.append(sub);
            continue;
        }

        if (fAllFromMe && fAllToMe)
        {
            // Payment to self
            CAmount nChange = wtx.getChange(colorId);
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::SendToSelf, "",
                            -(nDebit - nChange), nCredit - nChange));
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe)
        {
            //
            // Debit
            //
            CAmount nTxFee = std::get<1>(creditMap[ColorIdentifier()]) - wtx.tx->GetValueOut(ColorIdentifier());

            TransactionRecord sub(hash, nTime);
            sub.idx = i;
            sub.involvesWatchAddress = involvesWatchAddress;

            // Ignore parts sent to self, as this is usually the change
            // from a transaction sent back to our own address.
            if(wtx.txout_is_mine[i])
                continue;

            if (!boost::get<CNoDestination>(&wtx.txout_address[i]))
            {
                // Sent to Tapyrus Address
                if(colorId.type == TokenTypes::NONE)
                    sub.type = TransactionRecord::SendToAddress;
                else
                    sub.type = TransactionRecord::TokenSendToAddress;
                sub.address = EncodeDestination(wtx.txout_address[i]);
            }
            else
            {
                // Sent to IP, or other non-address transaction like OP_EVAL
                if(colorId.type == TokenTypes::NONE)
                    sub.type = TransactionRecord::SendToOther;
                else
                    sub.type = TransactionRecord::TokenSendToOther;
                sub.address = mapValue["to"];
            }

            CAmount nValue = nDebit;
            /* Add fee to first tpc output */
            if (nTxFee > 0 && colorId.type == TokenTypes::NONE)
            {
                nValue += nTxFee;
                nTxFee = 0;
            }
            sub.debit = -nValue;

            parts.append(sub);
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
