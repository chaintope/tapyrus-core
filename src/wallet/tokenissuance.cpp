// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// IssueReissuableToken/IssueToken/BurnToken moved out of wallet/rpcwallet.cpp
// (tapyrus_rpc) into tapyrus_wallet: interfaces/wallet.cpp (also
// tapyrus_wallet) calls all three directly, which is a real dependency on
// tapyrus_rpc that tapyrus_wallet never declares -- the same class of
// problem RegisterWalletRPCCommands/GetWalletForJSONRPCRequest had. None of
// the three touch anything RPC-request-shaped (they take CWallet*/CAmount/
// CCoinControl&, not JSONRPCRequest) despite returning UniValue/throwing
// JSONRPCError, so they belong at the tier both callers can already reach.
// wallet/rpcwallet.cpp's issuetoken()/reissuetoken() RPC handlers still call
// these, declared as before in wallet/rpcwallet.h.

#include <amount.h>
#include <coloridentifier.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <net.h>
#include <rpc/protocol.h>
#include <script/standard.h>
#include <util.h>
#include <utilmoneystr.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>

UniValue IssueReissuableToken(CWallet* const pwallet, const std::string& script, CAmount tokenValue, CCoinControl& coin_control)
{
    LOCK2(cs_main, pwallet->cs_wallet);

    // Pre-flight: resolve the tx2 output destination and top up the keypool
    // before committing tx1 to the network.  Any failure here returns a clean
    // error with no transaction broadcast.

    if (!pwallet->IsLocked())
        pwallet->TopUpKeyPool();

    // Reuse an existing CColorKeyID for this color if one is already in the
    // address book (happens on reissue). This prevents duplicate table entries.
    CTxDestination colorDest;
    bool foundExisting = false;
    for (const auto& entry : pwallet->mapAddressBook) {
        const CColorKeyID* existing = std::get_if<CColorKeyID>(&entry.first);
        if (existing && existing->color == coin_control.m_colorId) {
            colorDest = *existing;
            foundExisting = true;
            break;
        }
    }

    if (foundExisting) {
        // mapAddressBook can hold watch-only or externally-imported addresses.
        // Verify the wallet actually holds the signing key before committing.
        const CColorKeyID* ck = std::get_if<CColorKeyID>(&colorDest);
        if (!ck || !pwallet->HaveKey(ck->getKeyID())) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                "Cannot reuse colored address: wallet does not have the private key for the existing address");
        }
    } else {
        // Pre-allocate the key for tx2's colored output before tx1 is broadcast.
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

        CKeyID key_id = newKey.GetID();
        colorDest = CColorKeyID(key_id, coin_control.m_colorId);
    }

    // Verify the wallet is still unlocked before committing any transaction.
    if (pwallet->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");

    CTransactionRef tx1;
    //creating tx1
    {
        std::vector<unsigned char> vscript = ParseHex(script);
        CScript scriptPubKey(vscript.begin(), vscript.end());

        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;
        if (!ExtractDestinations(scriptPubKey, type, vDest, nRequired)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid Tapyrus script: ") + script);
        }
        for (const CTxDestination &dest : vDest)
            if(!IsValidDestination(dest))
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid Tapyrus script:") + script);

        // Create and send the transaction
        CReserveKey reservekey(pwallet);
        CAmount nFeeRequired;
        std::string strError;
        std::vector<CRecipient> vecSend;
        CWallet::ChangePosInOut mapChangePosRet;
        mapChangePosRet[ColorIdentifier()] = -1;
        // Use the wallet's configured fallback fee per KB as the anchor amount.
        // This ensures the output is well above dust and propagates across the network.
        CAmount minSpendableAmount = pwallet->m_fallback_fee.GetFeePerK();
        CRecipient recipient = {scriptPubKey, minSpendableAmount, false};
        vecSend.push_back(recipient);

        if (!pwallet->CreateTransaction(vecSend, tx1, reservekey, nFeeRequired, mapChangePosRet, strError, coin_control))
        {
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }
        CValidationState state;
        mapValue_t mapValue;
        if (!pwallet->CommitTransaction(tx1, std::move(mapValue), {} /* orderForm */, reservekey, g_connman.get(), state)) {
            strError = strprintf("Error: The transaction was rejected! Reason given: %s", FormatStateMessage(state));
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }
    }

    CTransactionRef tx2;
    //creating tx2
    {
        CScript scriptpubkey = GetScriptForDestination(colorDest);

        // Create and send the transaction
        CReserveKey reservekey(pwallet);
        CAmount nFeeRequired;
        std::string strError;
        std::vector<CRecipient> vecSend;
        CWallet::ChangePosInOut mapChangePosRet;
        mapChangePosRet[ColorIdentifier()] = -1;
        CRecipient recipient = {scriptpubkey, tokenValue, false};
        vecSend.push_back(recipient);
        COutPoint out(tx1->GetHashMalFix(), 0);
        coin_control.m_colorTxType = ColoredTxType::ISSUE;
        coin_control.Select(out);
        // included other TPC UTXOs to pay fees.
        coin_control.fAllowOtherInputs = true;

        if (!pwallet->CreateTransaction(vecSend, tx2, reservekey, nFeeRequired, mapChangePosRet, strError, coin_control))
        {
            pwallet->AbandonTransaction(tx1->GetHashMalFix());
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }
        CValidationState state;
        mapValue_t mapValue;
        if (!pwallet->CommitTransaction(tx2, std::move(mapValue), {} /* orderForm */, reservekey, g_connman.get(), state)) {
            pwallet->AbandonTransaction(tx1->GetHashMalFix());
            strError = strprintf("Error: The transaction was rejected! Reason given: %s", FormatStateMessage(state));
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }
    }

    // Register the address book entry only after both transactions commit successfully.
    if (!foundExisting)
        pwallet->SetAddressBook(colorDest, coin_control.m_colorId.toHexString(), "receive");

    UniValue result(UniValue::VOBJ);
    result.pushKV("color", coin_control.m_colorId.toHexString());
    result.pushKV("address", EncodeDestination(colorDest));
    UniValue txidlist(UniValue::VARR);
    txidlist.push_back(tx1->GetHashMalFix().GetHex());
    txidlist.push_back(tx2->GetHashMalFix().GetHex());
    result.pushKV("txids", txidlist);
    return result;
}

UniValue IssueToken(CWallet* const pwallet, CAmount tokenValue, CCoinControl& coin_control)
{
    LOCK2(cs_main, pwallet->cs_wallet);

    if (!pwallet->IsLocked()) {
        pwallet->TopUpKeyPool();
    }

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }

    CKeyID key_id = newKey.GetID();
    CTxDestination colorDest = CColorKeyID(key_id, coin_control.m_colorId);

    CScript scriptpubkey = GetScriptForDestination(colorDest);
    pwallet->SetAddressBook(colorDest, coin_control.m_colorId.toHexString(), "receive");

    // Create and send the transaction
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    std::vector<CRecipient> vecSend;
    CWallet::ChangePosInOut mapChangePosRet;
    mapChangePosRet[ColorIdentifier()] = -1;
    CRecipient recipient = {scriptpubkey, tokenValue, false};
    vecSend.push_back(recipient);
    CTransactionRef tx;

    if (!pwallet->CreateTransaction(vecSend, tx, reservekey, nFeeRequired, mapChangePosRet, strError, coin_control)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    CValidationState state;
    mapValue_t mapValue;
    if (!pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */, reservekey, g_connman.get(), state)) {
        strError = strprintf("Error: The transaction was rejected! Reason given: %s", FormatStateMessage(state));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("color", coin_control.m_colorId.toHexString());
    result.pushKV("address", EncodeDestination(colorDest));
    result.pushKV("txid", tx->GetHashMalFix().GetHex());
    return result;
}

CTransactionRef BurnToken(CWallet * const pwallet, const ColorIdentifier& colorId, CAmount nValue)
{
    mapValue_t mapValue;
    mapValue["comment"] = colorId.toHexString();

    if (!pwallet->IsLocked()) {
        pwallet->TopUpKeyPool();
    }

    // Burn = colored inputs, no colored output.  vecSend is intentionally empty;
    // m_burnAmount tells CreateTransaction how many tokens to select as inputs.
    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    std::vector<CRecipient> vecSend;
    CWallet::ChangePosInOut mapChangePosRet;
    mapChangePosRet[ColorIdentifier()] = -1;
    CTransactionRef tx;
    CCoinControl coin_control;
    coin_control.m_colorTxType = ColoredTxType::BURN;
    coin_control.m_colorId = colorId;
    coin_control.m_burnAmount = nValue;

    if (!pwallet->CreateTransaction(vecSend, tx, reservekey, nFeeRequired, mapChangePosRet, strError, coin_control)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    CValidationState state;
    if (!pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */, reservekey, g_connman.get(), state)) {
        strError = strprintf("Error: The transaction was rejected! Reason given: %s", FormatStateMessage(state));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    return tx;
}
