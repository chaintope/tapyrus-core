// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_WALLET_WALLETMESSAGES_H
#define TAPYRUS_WALLET_WALLETMESSAGES_H

#include <tinyformat.h>
#include <ui_interface.h>
#include <util.h>
#include <wallet/walletnotifications.h>

#include <cassert>
#include <string>

/** What the user reads about the wallet. Header-only, so the node can show these
 *  without linking the wallet. */
class WalletMessages
{
public:
    /** The fee option is set very high. */
    static std::string FeeSettingHigh(WalletFeeSetting setting)
    {
        switch (setting) {
        case WalletFeeSetting::MIN_RELAY_TX_FEE:
            return AmountHighWarn("-minrelaytxfee") + " " + _("The wallet will avoid paying less than the minimum relay fee.");
        case WalletFeeSetting::MIN_TX_FEE:
            return AmountHighWarn("-mintxfee") + " " + _("This is the minimum transaction fee you pay on every transaction.");
        case WalletFeeSetting::FALLBACK_FEE:
            return AmountHighWarn("-fallbackfee") + " " + _("This is the transaction fee you may pay when fee estimates are not available.");
        case WalletFeeSetting::DISCARD_FEE:
            return AmountHighWarn("-discardfee") + " " + _("This is the transaction fee you may discard if change is smaller than dust at this level");
        case WalletFeeSetting::PAY_TX_FEE:
            return AmountHighWarn("-paytxfee") + " " + _("This is the transaction fee you will pay if you send a transaction.");
        } // no default case, so the compiler can warn about missing cases
        assert(false);
        return "";
    }

    /** A corrupt wallet file was salvaged; the original was saved as backup_file in wallet_dir. */
    static std::string WalletFileSalvaged(const std::string& wallet_file, const std::string& backup_file, const std::string& wallet_dir)
    {
        return strprintf(_("Warning: Wallet file corrupt, data salvaged!"
                           " Original %s saved as %s in %s; if"
                           " your balance or transactions are incorrect you should"
                           " restore from a backup."),
            wallet_file, backup_file, wallet_dir);
    }

    /** The keys of wallet_file were read, but some transactions or address book entries were not. */
    static std::string TransactionDataUnreadable(const std::string& wallet_file)
    {
        return strprintf(_("Error reading %s! All keys read correctly, but transaction data"
                           " or address book entries might be missing or incorrect."),
            wallet_file);
    }

    /** wallet_file has private keys although it was created with them disabled. */
    static std::string PrivateKeysInDisabledWallet(const std::string& wallet_file)
    {
        return strprintf(_("Warning: Private keys detected in wallet {%s} with disabled private keys"), wallet_file);
    }
};

#endif // TAPYRUS_WALLET_WALLETMESSAGES_H
