// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Holds the one WalletInit method that needs the wallet RPCs
// (RegisterWalletRPCCommands(), in wallet/rpcwallet.cpp).

#include <util.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
#include <wallet/walletinit.h>

void WalletInit::RegisterRPC(CRPCTable &t) const
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
        return;
    }

    RegisterWalletRPCCommands(t);
}
