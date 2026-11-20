// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_WALLET_WALLETNOTIFICATIONS_H
#define TAPYRUS_WALLET_WALLETNOTIFICATIONS_H

#include <notificationsinterface.h>
#include <uint256.h>

#include <string>

/** A fee option the wallet reads; each has its own warning when set very high. */
enum class WalletFeeSetting {
    MIN_RELAY_TX_FEE,
    MIN_TX_FEE,
    FALLBACK_FEE,
    DISCARD_FEE,
    PAY_TX_FEE,
};

/** What the wallets publish. */
class WalletNotifications : public virtual NotificationsInterface
{
public:
    /** The fee option is set very high. */
    virtual void ReportFeeSettingHigh(WalletFeeSetting setting) = 0;
    /** A corrupt wallet file was salvaged; the original was saved as backup_file in wallet_dir. */
    virtual void ReportWalletFileSalvaged(const std::string& wallet_file, const std::string& backup_file, const std::string& wallet_dir) = 0;
    /** The keys of wallet_file were read, but some transactions or address book entries were not. */
    virtual void ReportTransactionDataUnreadable(const std::string& wallet_file) = 0;
    /** wallet_file has private keys although it was created with them disabled. */
    virtual void ReportPrivateKeysInDisabledWallet(const std::string& wallet_file) = 0;
    /** Rescanning wallet_name is percent done; 0 when it starts and 100 when it ends. */
    virtual void ReportRescanProgress(const std::string& wallet_name, int percent) = 0;
    /** A transaction of wallet_name was added or updated. */
    virtual void ReportTransactionChanged(const std::string& wallet_name, const uint256& txid) = 0;
};

#endif // TAPYRUS_WALLET_WALLETNOTIFICATIONS_H
