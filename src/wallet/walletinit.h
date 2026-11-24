// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_WALLET_WALLETINIT_H
#define TAPYRUS_WALLET_WALLETINIT_H

#include <wallet/walleteventqueue.h>
#include <walletinitinterface.h>

class WalletInit : public WalletInitInterface {
public:

    //! Return the wallets help message.
    void AddWalletOptions() const override;

    //! Wallets parameter interaction
    bool ParameterInteraction() const override;

    //! Register wallet RPCs.
    void RegisterRPC(CRPCTable &tableRPC) const override;

    //! Responsible for reading and validating the -wallet arguments and verifying the wallet database.
    //  This function will perform salvage on the wallet if requested, as long as only one wallet is
    //  being loaded (WalletParameterInteraction forbids -salvagewallet, -zapwallettxes or -upgradewallet with multiwallet).
    bool Verify() const override;

    void SetChainInterface(interfaces::Chain* chain) const override { m_chain = chain; }
    interfaces::Chain* GetChainInterface() const override { return m_chain; }
    WalletEvents* GetWalletEvents() const override { return &m_events; }

    //! Load wallet databases.
    bool Open() const override;

    //! Complete startup of wallets.
    void Start(CScheduler& scheduler) const override;

    //! Flush all wallets in preparation for shutdown.
    void Flush() const override;

    //! Stop all wallets. Wallets will be flushed first.
    void Stop() const override;

    //! Close all wallets.
    void Close() const override;

private:
    //! Mutable because the program holds WalletInit as a const reference.
    mutable interfaces::Chain* m_chain{nullptr};
    //! The wallet thread and its queue; started when the wallets open.
    mutable WalletEventQueue m_events;
};

#endif // TAPYRUS_WALLET_WALLETINIT_H
