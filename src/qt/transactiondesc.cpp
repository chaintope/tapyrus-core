// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifdef HAVE_CONFIG_H
#include <config/bitcoin-config.h>
#endif

#include <qt/transactiondesc.h>

#include <qt/tapyrusunits.h>
#include <qt/guiutil.h>
#include <qt/paymentserver.h>
#include <qt/transactionrecord.h>

#include <consensus/consensus.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <validation.h>
#include <script/script.h>
#include <timedata.h>
#include <util.h>
#include <wallet/db.h>
#include <wallet/wallet.h>
#include <policy/policy.h>

#include <stdint.h>
#include <string>

QString TransactionDesc::FormatTxStatus(const interfaces::WalletTx& wtx, const interfaces::WalletTxStatus& status, bool inMempool, int numBlocks, int64_t adjustedTime)
{
    if (!status.is_final)
    {
        if (wtx.tx->nLockTime < LOCKTIME_THRESHOLD)
            return tr("Open for %n more block(s)", "", wtx.tx->nLockTime - numBlocks);
        else
            return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.tx->nLockTime));
    }
    else
    {
        int nDepth = status.depth_in_main_chain;
        if (nDepth < 0)
            return tr("conflicted with a transaction with %1 confirmations").arg(-nDepth);
        else if (nDepth == 0)
            return tr("0/unconfirmed, %1").arg((inMempool ? tr("in memory pool") : tr("not in memory pool"))) + (status.is_abandoned ? ", "+tr("abandoned") : "");
        else
            return tr("%1 confirmations").arg(nDepth);
    }
}

QString TransactionDesc::toHTML(interfaces::Node& node, interfaces::Wallet& wallet, TransactionRecord *rec, int unit)
{
    int numBlocks;
    int64_t adjustedTime;
    interfaces::WalletTxStatus status;
    interfaces::WalletOrderForm orderForm;
    bool inMempool;
    interfaces::WalletTx wtx = wallet.getWalletTxDetails(node, rec->hash, status, orderForm, inMempool, numBlocks, adjustedTime);

    QString strHTML;

    strHTML.reserve(4000);
    strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

    std::set<ColorIdentifier> txColorIdSet{wtx.getAllColorIds(node)};

    std::map<const ColorIdentifier, std::tuple<CAmount, CAmount, CAmount> > creditMap;
    for(auto color : txColorIdSet)
    {
        CAmount nCredit = wtx.getCredit(color);
        CAmount nDebit = wtx.getDebit(color);
        CAmount nNet = nCredit - nDebit;
        creditMap.emplace(color, std::make_tuple(nCredit, nDebit, nNet) );
    }

    int64_t nTime = wtx.time;

    strHTML += "<b>" + tr("Status") + ":</b> " + FormatTxStatus(wtx, status, inMempool, numBlocks, adjustedTime);
    strHTML += "<br>";

    strHTML += "<b>" + tr("Date") + ":</b> " + (nTime ? GUIUtil::dateTimeStr(nTime) : "") + "<br>";

    //
    // From
    //
    if(!wtx.is_tokenInput && wtx.is_tokenOutput)
    {
        strHTML += "<b>" + tr("Source") + ":</b> " + tr("Token Issue") + "<br>";
    }
    else if (wtx.is_coinbase)
    {
        strHTML += "<b>" + tr("Source") + ":</b> " + tr("Generated") + "<br>";
    }
    else if (wtx.value_map.count("from") && !wtx.value_map["from"].empty())
    {
        // Online transaction
        strHTML += "<b>" + tr("From") + ":</b> " + GUIUtil::HtmlEscape(wtx.value_map["from"]) + "<br>";
    }

    //
    // To
    //
    if (wtx.value_map.count("to") && !wtx.value_map["to"].empty())
    {
        // Online transaction
        std::string strAddress = wtx.value_map["to"];
        strHTML += "<b>" + tr("To") + ":</b> ";
        CTxDestination dest = DecodeDestination(strAddress);
        std::string name;
        if (wallet.getAddress(
                dest, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr) && !name.empty())
            strHTML += GUIUtil::HtmlEscape(name) + " ";
        strHTML += GUIUtil::HtmlEscape(strAddress) + "<br>";
    }

    isminetype fAllFromMe = ISMINE_SPENDABLE;
    for (isminetype mine : wtx.txin_is_mine)
    {
        if(fAllFromMe > mine) fAllFromMe = mine;
    }

    isminetype fAllToMe = ISMINE_SPENDABLE;
    for (isminetype mine : wtx.txout_is_mine)
    {
        if(fAllToMe > mine) fAllToMe = mine;
    }
    if(fAllFromMe && ISMINE_WATCH_ONLY)
        strHTML += "<b>" + tr("From") + ":</b> " + tr("watch-only") + "<br>";

    for (const CTxOut& txout : wtx.tx->vout)
    {
        ColorIdentifier colorId = GetColorIdFromScript(txout.scriptPubKey);

        CAmount nCredit = std::get<0>(creditMap[colorId]);
        CAmount nDebit = std::get<1>(creditMap[colorId]);
        CAmount nNet = std::get<2>(creditMap[colorId]);

        //
        // Amount
        //
        if (wtx.is_coinbase)
        {
            //
            // Coinbase
            //
            strHTML += "<b>" + tr("Token") + ":</b> " + TapyrusUnits::longName(unit) + "<br>";
            strHTML += "<b>" + tr("Credit") + ":</b> ";
            if (status.is_in_main_chain)
                strHTML += TapyrusUnits::formatHtmlWithUnit(unit, nNet);
            else
                strHTML += "(" + tr("not accepted") + ")";
            strHTML += "<br>";
        }
        else if (nNet > 0)
        {
            //
            // Credit
            //
            strHTML += "<b>" + tr("Token") + ":</b> " + (colorId.type == TokenTypes::NONE ? TapyrusUnits::longName(unit) : colorId.toHexString().c_str()) + "<br>";
            strHTML += "<b>" + tr("Credit") + ":</b> " + (colorId.type == TokenTypes::NONE ?  TapyrusUnits::formatHtmlWithUnit(unit, nNet) : TapyrusUnits::formatHtmlWithUnit(TapyrusUnits::TOKEN, nNet)) + "<br>";

            // Offline transaction
            CTxDestination address = DecodeDestination(rec->address);
            if (IsValidDestination(address)) {
                std::string name;
                isminetype ismine;
                if (wallet.getAddress(address, &name, &ismine, /* purpose= */ nullptr))
                {
                    strHTML += "<b>" + tr("From") + ":</b> " + tr("unknown") + "<br>";
                    strHTML += "<b>" + tr("To") + ":</b> ";
                    strHTML += GUIUtil::HtmlEscape(rec->address);
                    QString addressOwned = ismine == ISMINE_SPENDABLE ? tr("own address") : tr("watch-only");
                    if (!name.empty())
                        strHTML += " (" + addressOwned + ", " + tr("label") + ": " + GUIUtil::HtmlEscape(name) + ")";
                    else
                        strHTML += " (" + addressOwned + ")";
                    strHTML += "<br>";
                }
            }
        }
        else if (fAllFromMe)
        {
            //
            // Debit
            //
            auto mine = wtx.txout_is_mine.begin();

            // Ignore change
            isminetype toSelf = *(mine++);
            //if ((toSelf == ISMINE_SPENDABLE) && (fAllFromMe == ISMINE_SPENDABLE))
            //    continue;

            if (!wtx.value_map.count("to") || wtx.value_map["to"].empty())
            {
                // Offline transaction
                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address))
                {
                    strHTML += "<b>" + tr("To") + ":</b> ";
                    std::string name;
                    if (wallet.getAddress(
                            address, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr) && !name.empty())
                        strHTML += GUIUtil::HtmlEscape(name) + " ";
                    strHTML += GUIUtil::HtmlEscape(EncodeDestination(address));
                    if(toSelf == ISMINE_SPENDABLE)
                        strHTML += " (own address)";
                    else if(toSelf & ISMINE_WATCH_ONLY)
                        strHTML += " (watch-only)";
                    strHTML += "<br>";
                }
            }

            strHTML += "<b>" + tr("Token") + ":</b> " + (colorId.type == TokenTypes::NONE ? TapyrusUnits::longName(unit) : colorId.toHexString().c_str()) + "<br>";
            strHTML += "<b>" + tr("Debit") + ":</b> " + (colorId.type == TokenTypes::NONE ?  TapyrusUnits::formatHtmlWithUnit(unit, -nDebit) : TapyrusUnits::formatHtmlWithUnit(TapyrusUnits::TOKEN, -nDebit) )+ "<br>";
            if(toSelf)
                strHTML += "<b>" + tr("Credit") + ":</b> " + (colorId.type == TokenTypes::NONE ?  TapyrusUnits::formatHtmlWithUnit(unit, nCredit) : TapyrusUnits::formatHtmlWithUnit(TapyrusUnits::TOKEN, nCredit) ) + "<br>";
        }
        else
        {
            //
            // Mixed debit transaction
            //
            auto mine = wtx.txin_is_mine.begin();
            for (const CTxIn& txin : wtx.tx->vin) {
                if (*(mine++)) {
                    strHTML += "<b>" + tr("Debit") + ":</b> " + (colorId.type == TokenTypes::NONE ? TapyrusUnits::formatHtmlWithUnit(unit, -wallet.getDebit(txin, ISMINE_ALL)) : TapyrusUnits::formatHtmlWithUnit(TapyrusUnits::TOKEN, -wallet.getDebit(txin, ISMINE_ALL))) + "<br>";
                }
            }
            mine = wtx.txout_is_mine.begin();
            for (const CTxOut& txout : wtx.tx->vout) {
                if (*(mine++)) {
                    strHTML += "<b>" + tr("Credit") + ":</b> " + (colorId.type == TokenTypes::NONE ? TapyrusUnits::formatHtmlWithUnit(unit, wallet.getCredit(txout, ISMINE_ALL)) : TapyrusUnits::formatHtmlWithUnit(TapyrusUnits::TOKEN, wallet.getCredit(txout, ISMINE_ALL))) + "<br>";
                }
            }
        }
    }


    CAmount nTxFee = std::get<1>(creditMap[ColorIdentifier()]) - wtx.tx->GetValueOut(ColorIdentifier());
    if (nTxFee > 0)
        strHTML += "<br><b>" + tr("Transaction fee") + ":</b> " + TapyrusUnits::formatHtmlWithUnit(unit, -nTxFee) + "<br>";

    //
    // Message
    //
    if (wtx.value_map.count("message") && !wtx.value_map["message"].empty())
        strHTML += "<br><b>" + tr("Message") + ":</b><br>" + GUIUtil::HtmlEscape(wtx.value_map["message"], true) + "<br>";
    if (wtx.value_map.count("comment") && !wtx.value_map["comment"].empty())
        strHTML += "<br><b>" + tr("Comment") + ":</b><br>" + GUIUtil::HtmlEscape(wtx.value_map["comment"], true) + "<br>";

    strHTML += "<b>" + tr("Transaction ID") + ":</b> " + rec->getTxHash() + "<br>";
    strHTML += "<b>" + tr("Transaction total size") + ":</b> " + QString::number(wtx.tx->GetTotalSize()) + " bytes<br>";
    strHTML += "<b>" + tr("Transaction virtual size") + ":</b> " + QString::number(GetVirtualTransactionSize(*wtx.tx)) + " bytes<br>";
    strHTML += "<b>" + tr("Output index") + ":</b> " + QString::number(rec->getOutputIndex()) + "<br>";

    // Message from normal bitcoin:URI (bitcoin:123...?message=example)
    for (const std::pair<std::string, std::string>& r : orderForm)
        if (r.first == "Message")
            strHTML += "<br><b>" + tr("Message") + ":</b><br>" + GUIUtil::HtmlEscape(r.second, true) + "<br>";

    if (wtx.is_coinbase)
    {
        strHTML += "<br>" + tr(" When you generated this block, it was broadcast to the network to be added to the block chain. If it fails to get into the chain, its state will change to \"not accepted\" and it won't be spendable. This may occasionally happen if another node generates a block within a few seconds of yours.") + "<br>";
    }

    //
    // Debug view
    //
    if (node.getLogCategories() != BCLog::NONE)
    {
        strHTML += "<hr><br>" + tr("Debug information") + "<br><br>";
        for (const CTxIn& txin : wtx.tx->vin)
            if(wallet.txinIsMine(txin))
                strHTML += "<b>" + tr("Debit") + ":</b> " + TapyrusUnits::formatHtmlWithUnit(unit, -wallet.getDebit(txin, ISMINE_ALL)) + "<br>";
        for (const CTxOut& txout : wtx.tx->vout)
            if(wallet.txoutIsMine(txout))
                strHTML += "<b>" + tr("Credit") + ":</b> " + TapyrusUnits::formatHtmlWithUnit(unit, wallet.getCredit(txout, ISMINE_ALL)) + "<br>";

        strHTML += "<br><b>" + tr("Transaction") + ":</b><br>";
        strHTML += GUIUtil::HtmlEscape(wtx.tx->ToString(), true);

        strHTML += "<br><b>" + tr("Inputs") + ":</b>";
        strHTML += "<ul>";

        for (const CTxIn& txin : wtx.tx->vin)
        {
            COutPoint prevout = txin.prevout;

            Coin prev;
            if(node.getUnspentOutput(prevout, prev))
            {
                {
                    strHTML += "<li>";
                    const CTxOut &vout = prev.out;
                    CTxDestination address;
                    if (ExtractDestination(vout.scriptPubKey, address))
                    {
                        std::string name;
                        if (wallet.getAddress(address, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr) && !name.empty())
                            strHTML += GUIUtil::HtmlEscape(name) + " ";
                        strHTML += QString::fromStdString(EncodeDestination(address));
                    }
                    strHTML = strHTML + " " + tr("Amount") + "=" + TapyrusUnits::formatHtmlWithUnit(unit, vout.nValue);
                    strHTML = strHTML + " IsMine=" + (wallet.txoutIsMine(vout) & ISMINE_SPENDABLE ? tr("true") : tr("false")) + "</li>";
                    strHTML = strHTML + " IsWatchOnly=" + (wallet.txoutIsMine(vout) & ISMINE_WATCH_ONLY ? tr("true") : tr("false")) + "</li>";
                }
            }
        }

        strHTML += "</ul>";
    }

    strHTML += "</font></html>";
    return strHTML;
}
