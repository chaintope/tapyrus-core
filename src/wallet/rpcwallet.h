// Copyright (c) 2016-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_RPCWALLET_H
#define BITCOIN_WALLET_RPCWALLET_H

#include <amount.h>
#include <coloridentifier.h>
#include <primitives/transaction.h>
#include <string>

class CRPCTable;
class CWallet;
class CCoinControl;
class JSONRPCRequest;
class UniValue;
struct PartiallySignedTransaction;
class CTransaction;

void RegisterWalletRPCCommands(CRPCTable &t);

/**
 * Figures out what wallet, if any, to use for a JSONRPCRequest.
 *
 * @param[in] request JSONRPCRequest that wishes to access a wallet
 * @return nullptr if no wallet should be used, or a pointer to the CWallet
 */
std::shared_ptr<CWallet> GetWalletForJSONRPCRequest(const JSONRPCRequest& request);

std::string HelpRequiringPassphrase(CWallet *);
void EnsureWalletIsUnlocked(CWallet *);
bool EnsureWalletIsAvailable(CWallet *, bool avoidException);

UniValue getaddressinfo(const JSONRPCRequest& request);
UniValue signrawtransactionwithwallet(const JSONRPCRequest& request);
bool FillPSBT(const CWallet* pwallet, PartiallySignedTransaction& psbtx, const CTransaction* txConst, int sighash_type = 1, bool sign = true, bool bip32derivs = false);

// Token operation helpers — also called from interfaces/wallet.cpp (GUI layer).
UniValue IssueReissuableToken(CWallet* const pwallet, const std::string& script, CAmount tokenValue, CCoinControl& coin_control);
UniValue IssueToken(CWallet* const pwallet, CAmount tokenValue, CCoinControl& coin_control);
CTransactionRef BurnToken(CWallet* const pwallet, const ColorIdentifier& colorId, CAmount nValue);

#endif //BITCOIN_WALLET_RPCWALLET_H
