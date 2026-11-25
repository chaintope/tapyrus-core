// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_NOTIFICATIONHUB_H
#define TAPYRUS_NOTIFICATIONHUB_H

#include <notifications.h>
#include <sync.h>

#include <chrono>
#include <map>
#include <string>
#include <vector>

/**
 * What the components in this process publish to. It keeps the current message
 * of each warning for the GUI and RPC to read, and passes every publish on to
 * its subscribers, in the order they subscribed. NodeContext owns it; init
 * subscribes the error dialog and the shutdown.
 */
class NotificationHub : public Notifications
{
public:
    void Subscribe(Notifications& subscriber);
    void Unsubscribe(Notifications& subscriber);

    /** Records the fatal error as a warning and logs it. */
    void ReportFatalError(FatalErrorReason reason, const std::string& detail) override;
    /** Sets the clock warning, telling the user how to fix their clock. */
    void ReportClockOutOfSync(std::chrono::minutes threshold) override;
    void ReportClockInSync() override;
    /** Sets the fork warning, running -alertnotify once per fork. */
    void ReportLargeWorkFork(const uint256& fork_base) override;
    void ReportInvalidChainAhead() override;
    void ReportForkResolved() override;
    /** Logs the stop; it is not a warning. */
    void ReportStopHeightReached(int height) override;
    /** Shows the upgrade's progress in the GUI. */
    void ReportUpgradeProgress(int percent) override;
    /** Each of the four below shows its warning to the user as a start-up warning. */
    void ReportFeeSettingHigh(WalletFeeSetting setting) override;
    void ReportWalletFileSalvaged(const std::string& wallet_file, const std::string& backup_file, const std::string& wallet_dir) override;
    void ReportTransactionDataUnreadable(const std::string& wallet_file) override;
    void ReportPrivateKeysInDisabledWallet(const std::string& wallet_file) override;
    /** Runs the -walletnotify command with the transaction id. */
    void ReportTransactionChanged(const std::string& wallet_name, const uint256& txid) override;

    /** Format a string that describes several potential problems detected by the core.
     * @param[in] strFor can have the following values:
     * - "statusbar": get the most important warning
     * - "gui": get all warnings, translated (where possible) for GUI, separated by <hr />
     * @returns the warning string selected by strFor
     */
    std::string GetWarnings(const std::string& strFor) const;

    /** What the log and the warnings say about the fatal error. */
    static std::string FatalErrorMessage(FatalErrorReason reason, const std::string& detail);

private:
    //! What a warning is about; each has at most one current message.
    enum class WarningId {
        FATAL_ERROR,
        LARGE_WORK_FORK,
        LARGE_WORK_INVALID_CHAIN,
        CLOCK_OUT_OF_SYNC,
    };

    //! Each tells the GUI when the stored messages change, and returns whether they did.
    bool Store(WarningId id, const std::string& message);
    bool Erase(WarningId id);
    //! Runs the -alertnotify command with the message.
    void AlertNotify(const std::string& message) const;
    //! Taken out of the lock before calling them, so a subscriber may publish in turn.
    std::vector<Notifications*> Subscribers() const;

    mutable Mutex cs_warnings;
    //! The current message of each warning, in increasing statusbar priority.
    std::map<WarningId, std::string> m_messages GUARDED_BY(cs_warnings);
    std::vector<Notifications*> m_subscribers GUARDED_BY(cs_warnings);
};

/** Shows a fatal error to the user. */
class FatalErrorDialog : public Notifications
{
public:
    static FatalErrorDialog& Instance();
    void ReportFatalError(FatalErrorReason reason, const std::string& detail) override;
};

/** Shuts the node down on a fatal error and at the stop height. */
class NodeShutdown : public Notifications
{
public:
    static NodeShutdown& Instance();
    void ReportFatalError(FatalErrorReason reason, const std::string& detail) override;
    void ReportStopHeightReached(int height) override;
};

#endif // TAPYRUS_NOTIFICATIONHUB_H
