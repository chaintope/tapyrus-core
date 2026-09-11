// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Split out of wallet/init.cpp so that this one call into
// RegisterWalletRPCCommands() -- defined in wallet/rpcwallet.cpp, which
// compiles into tapyrus_rpc -- doesn't force all of WalletInit into
// tapyrus_rpc. Everything else WalletInit does stays in wallet/init.cpp
// (tapyrus_wallet). See the dependency graph comment at the top of the
// top-level src/CMakeLists.txt.

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
