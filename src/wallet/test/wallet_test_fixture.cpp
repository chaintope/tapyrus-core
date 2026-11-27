// Copyright (c) 2016-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/test/wallet_test_fixture.h>

#include <rpc/rpctable.h>
#include <validationinterface.h>
#include <wallet/db.h>
#include <wallet/rpcwallet.h>

WalletTestingSetup::WalletTestingSetup(const std::string& chainName):
    TestingSetup(chainName), m_wallet(m_chain.get(), "mock", WalletDatabase::CreateMock())
{
    bool fFirstRun;
    m_wallet.LoadWallet(fFirstRun);
    m_wallet_events.Start();
    m_wallet_events.Register(m_wallet);

    RegisterWalletRPCCommands(tableRPC);
}

WalletTestingSetup::~WalletTestingSetup()
{
    m_wallet_events.Unregister(m_wallet);
    // Stop does not wait for a node callback already inside the queue, so let those finish first.
    SyncWithValidationInterfaceQueue();
    m_wallet_events.Stop();
}
