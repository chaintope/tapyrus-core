// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Holds the one WalletInit method that needs tapyrus_rpc
// (RegisterWalletRPCCommands(), in wallet/rpcwallet.cpp). Neither this
// file nor wallet/init.cpp compiles into a library -- both are glue; see
// tapyrusd's add_executable() in src/CMakeLists.txt.

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
