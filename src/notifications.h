// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_NOTIFICATIONS_H
#define TAPYRUS_NOTIFICATIONS_H

#include <chainnotifications.h>
#include <index/indexnotifications.h>
#include <peernotifications.h>
#include <wallet/walletnotifications.h>

#include <chrono>
#include <string>

/** Everything the components publish, each call doing nothing unless overridden:
 *  what the node's NotificationHub and its subscribers implement. */
class Notifications : public ChainNotifications, public PeerNotifications, public IndexNotifications, public WalletNotifications
{
public:
    void ReportFatalError(FatalErrorReason reason, const std::string& detail) override {}
    void ReportClockOutOfSync(std::chrono::minutes threshold) override {}
    void ReportClockInSync() override {}
    void ReportLargeWorkFork(const uint256& fork_base) override {}
    void ReportInvalidChainAhead() override {}
    void ReportForkResolved() override {}
    void ReportStopHeightReached(int height) override {}
    void ReportUpgradeProgress(int percent) override {}
    void ReportFeeSettingHigh(WalletFeeSetting setting) override {}
    void ReportWalletFileSalvaged(const std::string& wallet_file, const std::string& backup_file, const std::string& wallet_dir) override {}
    void ReportTransactionDataUnreadable(const std::string& wallet_file) override {}
    void ReportPrivateKeysInDisabledWallet(const std::string& wallet_file) override {}
    void ReportRescanProgress(const std::string& wallet_name, int percent) override {}
    void ReportTransactionChanged(const std::string& wallet_name, const uint256& txid) override {}
};

#endif // TAPYRUS_NOTIFICATIONS_H
