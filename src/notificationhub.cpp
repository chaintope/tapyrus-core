// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <notificationhub.h>

#include <clientversion.h>
#include <shutdown.h>
#include <sync.h>
#include <ui_interface.h>
#include <util.h>
#include <utilstrencodings.h>

#include <algorithm>
#include <cassert>
#include <thread>

#include <boost/algorithm/string/replace.hpp>

void NotificationHub::Subscribe(Notifications& subscriber)
{
    LOCK(cs_warnings);
    m_subscribers.push_back(&subscriber);
}

void NotificationHub::Unsubscribe(Notifications& subscriber)
{
    LOCK(cs_warnings);
    m_subscribers.erase(std::remove(m_subscribers.begin(), m_subscribers.end(), &subscriber), m_subscribers.end());
}

std::vector<Notifications*> NotificationHub::Subscribers() const
{
    LOCK(cs_warnings);
    return m_subscribers;
}

bool NotificationHub::Store(WarningId id, const std::string& message)
{
    {
        LOCK(cs_warnings);
        auto [it, inserted] = m_messages.try_emplace(id, message);
        if (!inserted) {
            if (it->second == message) return false;
            it->second = message;
        }
    }
    // Readers show the warnings, so they are told about every change.
    uiInterface.NotifyAlertChanged();
    return true;
}

bool NotificationHub::Erase(WarningId id)
{
    {
        LOCK(cs_warnings);
        if (m_messages.erase(id) == 0) return false;
    }
    uiInterface.NotifyAlertChanged();
    return true;
}

void NotificationHub::AlertNotify(const std::string& message) const
{
    std::string strCmd = gArgs.GetArg("-alertnotify", "");
    if (strCmd.empty()) return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(message);
    safeStatus = singleQuote+safeStatus+singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    std::thread t(runCommand, strCmd);
    t.detach(); // thread runs free
}

void NotificationHub::ReportFatalError(FatalErrorReason reason, const std::string& detail)
{
    const std::string message{FatalErrorMessage(reason, detail)};
    Store(WarningId::FATAL_ERROR, message);
    LogPrintf("*** %s\n", message);
    for (Notifications* subscriber : Subscribers()) subscriber->ReportFatalError(reason, detail);
}

void NotificationHub::ReportClockOutOfSync(std::chrono::minutes threshold)
{
    const std::string message{strprintf(_(
        "Your computer's date and time appear to be more than %d minutes out of sync with the network, "
        "this may lead to consensus failure. After you've confirmed your computer's clock, this message "
        "should no longer appear when you restart your node. Without a restart, it should stop showing "
        "automatically after you've connected to a sufficient number of new outbound peers, which may "
        "take some time. You can inspect the `timeoffset` field of the `getpeerinfo` and `getnetworkinfo` "
        "RPC methods to get more info."
    ), threshold.count())};
    LogPrintf("Warning: %s\n", message);
    Store(WarningId::CLOCK_OUT_OF_SYNC, message);
    for (Notifications* subscriber : Subscribers()) subscriber->ReportClockOutOfSync(threshold);
}

void NotificationHub::ReportClockInSync()
{
    Erase(WarningId::CLOCK_OUT_OF_SYNC);
    for (Notifications* subscriber : Subscribers()) subscriber->ReportClockInSync();
}

void NotificationHub::ReportLargeWorkFork(const uint256& fork_base)
{
    // While the warning is set, this fork has been announced.
    if (Store(WarningId::LARGE_WORK_FORK, "Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.")) {
        AlertNotify("'Warning: Large-work fork detected, forking after block " + fork_base.ToString() + "'");
    }
    for (Notifications* subscriber : Subscribers()) subscriber->ReportLargeWorkFork(fork_base);
}

void NotificationHub::ReportInvalidChainAhead()
{
    Store(WarningId::LARGE_WORK_INVALID_CHAIN, "Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    for (Notifications* subscriber : Subscribers()) subscriber->ReportInvalidChainAhead();
}

void NotificationHub::ReportForkResolved()
{
    Erase(WarningId::LARGE_WORK_FORK);
    Erase(WarningId::LARGE_WORK_INVALID_CHAIN);
    for (Notifications* subscriber : Subscribers()) subscriber->ReportForkResolved();
}

void NotificationHub::ReportStopHeightReached(int height)
{
    LogPrintf("Stopping at height %d as -stopatheight asks\n", height);
    for (Notifications* subscriber : Subscribers()) subscriber->ReportStopHeightReached(height);
}

void NotificationHub::ReportUpgradeProgress(int percent)
{
    if (percent < 100) {
        uiInterface.ShowProgress(_("Upgrading txindex database"), percent, true);
    } else {
        uiInterface.ShowProgress("", 100, false);
    }
    for (Notifications* subscriber : Subscribers()) subscriber->ReportUpgradeProgress(percent);
}

std::string NotificationHub::GetWarnings(const std::string& strFor) const
{
    std::string strStatusBar;
    std::string strGUI;
    const std::string uiAlertSeperator = "<hr />";

    LOCK(cs_warnings);

    if (!CLIENT_VERSION_IS_RELEASE) {
        strStatusBar = "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications";
        strGUI = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");
    }

    // Later warnings take the status bar; the GUI shows them all.
    for (const auto& [id, message] : m_messages) {
        // A large-work fork already says the network disagrees.
        if (id == WarningId::LARGE_WORK_INVALID_CHAIN && m_messages.count(WarningId::LARGE_WORK_FORK)) continue;
        strStatusBar = message;
        // The fork texts are stored in English, so the GUI translates them; the others are stored as shown.
        const bool fork{id == WarningId::LARGE_WORK_FORK || id == WarningId::LARGE_WORK_INVALID_CHAIN};
        strGUI += (strGUI.empty() ? "" : uiAlertSeperator) + (fork ? _(message.c_str()) : message);
    }

    if (strFor == "gui")
        return strGUI;
    else if (strFor == "statusbar")
        return strStatusBar;
    assert(!"GetWarnings(): invalid parameter");
    return "error";
}

std::string NotificationHub::FatalErrorMessage(FatalErrorReason reason, const std::string& detail)
{
    switch (reason) {
    case FatalErrorReason::DISK_SPACE_LOW: return "Disk space is low!";
    case FatalErrorReason::BLOCK_WRITE_FAILED: return "Failed to write block";
    case FatalErrorReason::UNDO_WRITE_FAILED: return "Failed to write undo data";
    case FatalErrorReason::BLOCK_FILE_FLUSH_FAILED: return "Flushing block file to disk failed. This is likely the result of an I/O error.";
    case FatalErrorReason::BLOCK_INDEX_WRITE_FAILED: return "Failed to write to block index database";
    case FatalErrorReason::COINS_WRITE_FAILED: return "Failed to write to coin database";
    case FatalErrorReason::COINS_READ_FAILED: return "Error reading from database: " + detail;
    case FatalErrorReason::CORRUPT_BLOCK: return "Corrupt block found indicating potential hardware failure; shutting down";
    case FatalErrorReason::BLOCK_READ_FAILED: return "Failed to read block";
    case FatalErrorReason::SYSTEM_ERROR: return "System error: " + detail;
    case FatalErrorReason::FLUSH_SYSTEM_ERROR: return "System error while flushing: " + detail;
    case FatalErrorReason::INDEX_BLOCK_READ_FAILED: return "Failed to read block " + detail + " from disk";
    case FatalErrorReason::INDEX_BLOCK_WRITE_FAILED: return "Failed to write block " + detail + " to index database";
    case FatalErrorReason::INDEX_FIRST_BLOCK_NOT_GENESIS: return "First block connected is not the genesis block (height=" + detail + ")";
    case FatalErrorReason::INDEX_LOCATOR_BLOCK_NOT_FOUND: return "First block (hash=" + detail + ") in locator was not found";
    case FatalErrorReason::INDEX_INIT_FAILED: return detail + " failed to initialize";
    } // no default case, so the compiler can warn about missing cases
    assert(false);
    return "";
}

FatalErrorDialog& FatalErrorDialog::Instance()
{
    static FatalErrorDialog instance;
    return instance;
}

void FatalErrorDialog::ReportFatalError(FatalErrorReason reason, const std::string& detail)
{
    std::string message;
    switch (reason) {
    case FatalErrorReason::DISK_SPACE_LOW: message = _("Error: Disk space is low!"); break;
    case FatalErrorReason::COINS_READ_FAILED: message = _("Error reading from database, shutting down."); break;
    default: message = _("Error: A fatal internal error occurred, see debug.log for details");
    }
    uiInterface.ThreadSafeMessageBox(message, "", CClientUIInterface::MSG_ERROR);
}

NodeShutdown& NodeShutdown::Instance()
{
    static NodeShutdown instance;
    return instance;
}

void NodeShutdown::ReportFatalError(FatalErrorReason reason, const std::string& detail)
{
    StartShutdown();
}

void NodeShutdown::ReportStopHeightReached(int height)
{
    StartShutdown();
}
