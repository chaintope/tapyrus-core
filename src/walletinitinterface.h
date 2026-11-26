// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLETINITINTERFACE_H
#define BITCOIN_WALLETINITINTERFACE_H

#include <string>

class CRPCTable;
class WalletEvents;

namespace interfaces {
class Chain;
} // namespace interfaces

class WalletInitInterface {
public:
    /** Get wallet help string */
    virtual void AddWalletOptions() const = 0;
    /** Check wallet parameter interaction */
    virtual bool ParameterInteraction() const = 0;
    /** Register wallet RPC*/
    virtual void RegisterRPC(CRPCTable &) const = 0;
    /** Verify wallets */
    virtual bool Verify() const = 0;
    /** Set the node the wallets read from; null when they run without one. Must outlive the wallets. */
    virtual void SetChainInterface(interfaces::Chain* chain) const = 0;
    /** The node set above, for wallets loaded later */
    virtual interfaces::Chain* GetChainInterface() const = 0;
    /** The queue the wallet thread delivers the wallets' validation events from; null without wallets */
    virtual WalletEvents* GetWalletEvents() const = 0;
    /** Waits until the wallet thread has delivered the events queued before the call */
    virtual void SyncWalletEvents() const = 0;
    /** Open wallets*/
    virtual bool Open() const = 0;
    /** Start wallets*/
    virtual void Start() const = 0;
    /** Flush Wallets*/
    virtual void Flush() const = 0;
    /** Stop Wallets*/
    virtual void Stop() const = 0;
    /** Close wallets */
    virtual void Close() const = 0;

    virtual ~WalletInitInterface() {}
};

extern const WalletInitInterface& g_wallet_init_interface;

#endif // BITCOIN_WALLETINITINTERFACE_H
