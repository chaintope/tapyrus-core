// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>
#include <notificationhub.h>
#include <test/test_tapyrus.h>
#include <ui_interface.h>

#include <boost/signals2/connection.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {
const std::string FORK_WARNING{"Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues."};
const std::string INVALID_CHAIN_WARNING{"Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade."};
const std::string PRE_RELEASE{"This is a pre-release test build - use at your own risk - do not use for mining or merchant applications"};

//! The GUI text for these messages, after the pre-release notice a test build may show.
std::string GuiText(const std::string& messages)
{
    if (CLIENT_VERSION_IS_RELEASE) return messages;
    return messages.empty() ? PRE_RELEASE : PRE_RELEASE + "<hr />" + messages;
}

//! The lines -alertnotify wrote to the file, once at least count are there or 10 seconds have passed.
std::vector<std::string> AlertLines(const fs::path& file, size_t count)
{
    std::vector<std::string> lines;
    for (int32_t tries = 0; tries < 100; ++tries) {
        lines.clear();
        std::ifstream in{file.string()};
        for (std::string line; std::getline(in, line);) lines.push_back(line);
        if (lines.size() >= count) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    return lines;
}

//! The clock warning for a threshold of 10 minutes, as the hub words it.
std::string ClockWarning()
{
    NotificationHub notifications;
    notifications.ReportClockOutOfSync(std::chrono::minutes{10});
    return notifications.GetWarnings("statusbar");
}

//! Writes each call it hears, prefixed by its name, into a shared log.
class Recorder : public Notifications
{
public:
    Recorder(std::string name, std::vector<std::string>& log) : m_name(std::move(name)), m_log(log) {}

    void ReportShutdown(ShutdownReason reason, const std::string& detail) override { m_log.push_back(m_name + " shutdown " + NotificationHub::ShutdownMessage(reason, detail)); }
    void ReportClockOutOfSync(std::chrono::minutes threshold) override { m_log.push_back(m_name + " clock " + std::to_string(threshold.count())); }
    void ReportClockInSync() override { m_log.push_back(m_name + " clock ok"); }
    void ReportLargeWorkFork(const uint256& fork_base) override { m_log.push_back(m_name + " fork " + fork_base.ToString()); }
    void ReportInvalidChainAhead() override { m_log.push_back(m_name + " invalid chain"); }
    void ReportForkResolved() override { m_log.push_back(m_name + " fork resolved"); }
    void ReportUpgradeProgress(int percent) override { m_log.push_back(m_name + " upgrade " + std::to_string(percent)); }

private:
    const std::string m_name;
    std::vector<std::string>& m_log;
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(notificationhub_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(later_warnings_take_the_status_bar)
{
    NotificationHub notifications;
    notifications.ReportShutdown(ShutdownReason::SYSTEM_ERROR, "disk full");
    notifications.ReportClockOutOfSync(std::chrono::minutes{10});
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), ClockWarning());
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText("System error: disk full<hr />" + ClockWarning()));

    notifications.ReportClockInSync();
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), "System error: disk full");
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText("System error: disk full"));
}

BOOST_AUTO_TEST_CASE(a_large_work_fork_hides_the_invalid_chain_warning)
{
    NotificationHub notifications;
    notifications.ReportInvalidChainAhead();
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), INVALID_CHAIN_WARNING);
    notifications.ReportLargeWorkFork(uint256S("01"));
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), FORK_WARNING);
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(FORK_WARNING));
    // The fork stays announced while the chain reports only the invalid chain.
    notifications.ReportInvalidChainAhead();
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), FORK_WARNING);
}

BOOST_AUTO_TEST_CASE(readers_are_told_only_about_changes)
{
    NotificationHub notifications;
    int changes{0};
    const boost::signals2::scoped_connection counter{uiInterface.NotifyAlertChanged.connect([&changes] { ++changes; })};

    notifications.ReportClockOutOfSync(std::chrono::minutes{10});
    notifications.ReportClockOutOfSync(std::chrono::minutes{10});
    BOOST_CHECK_EQUAL(changes, 1);
    notifications.ReportClockOutOfSync(std::chrono::minutes{20});
    BOOST_CHECK_EQUAL(changes, 2);
    notifications.ReportClockInSync();
    notifications.ReportClockInSync();
    BOOST_CHECK_EQUAL(changes, 3);
}

BOOST_AUTO_TEST_CASE(a_clock_out_of_sync_names_the_threshold)
{
    NotificationHub notifications;
    notifications.ReportClockOutOfSync(std::chrono::minutes{10});
    BOOST_CHECK(notifications.GetWarnings("statusbar").find("more than 10 minutes out of sync") != std::string::npos);
    notifications.ReportClockInSync();
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(""));
}

BOOST_AUTO_TEST_CASE(the_chain_reports_set_and_clear_the_fork_warnings)
{
    NotificationHub notifications;
    notifications.ReportInvalidChainAhead();
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), INVALID_CHAIN_WARNING);
    notifications.ReportLargeWorkFork(uint256S("01"));
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), FORK_WARNING);
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(FORK_WARNING));
    notifications.ReportForkResolved();
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(""));
}

BOOST_AUTO_TEST_CASE(alertnotify_runs_once_per_fork)
{
    const fs::path file{GetDataDir() / "alert.txt"};
    gArgs.ForceSetArg("-alertnotify", "echo %s >> " + file.string());
    NotificationHub notifications;

    notifications.ReportLargeWorkFork(uint256S("01"));
    notifications.ReportLargeWorkFork(uint256S("01"));
    notifications.ReportInvalidChainAhead();
    BOOST_CHECK_EQUAL(AlertLines(file, 1).size(), 1U);
    notifications.ReportForkResolved();
    notifications.ReportLargeWorkFork(uint256S("02"));
    const std::vector<std::string> lines{AlertLines(file, 2)};
    // A second run for the first fork would have written by now.
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    BOOST_CHECK_EQUAL(AlertLines(file, 2).size(), 2U);
    // Before any requirement can end the case, so later tests run no command.
    gArgs.ForceSetArg("-alertnotify", "");
    BOOST_REQUIRE_EQUAL(lines.size(), 2U);
    BOOST_CHECK(lines[0].find(uint256S("01").ToString()) != std::string::npos);
    BOOST_CHECK(lines[1].find(uint256S("02").ToString()) != std::string::npos);
}

BOOST_AUTO_TEST_CASE(a_fatal_shutdown_is_stored_and_passed_on)
{
    std::vector<std::string> log;
    Recorder recorder{"r", log};
    NotificationHub notifications;
    notifications.Subscribe(recorder);
    notifications.ReportShutdown(ShutdownReason::INDEX_BLOCK_READ_FAILED, "00ab");
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), "Failed to read block 00ab from disk");
    const std::vector<std::string> expected{"r shutdown Failed to read block 00ab from disk"};
    BOOST_CHECK_EQUAL_COLLECTIONS(log.begin(), log.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(every_shutdown_reason_has_a_message)
{
    for (int32_t reason = 0; reason <= static_cast<int32_t>(ShutdownReason::INDEX_INIT_FAILED); ++reason) {
        BOOST_CHECK(!NotificationHub::ShutdownMessage(static_cast<ShutdownReason>(reason), "detail").empty());
    }
    BOOST_CHECK_EQUAL(NotificationHub::ShutdownMessage(ShutdownReason::DISK_SPACE_LOW, ""), "Disk space is low!");
    BOOST_CHECK_EQUAL(NotificationHub::ShutdownMessage(ShutdownReason::SYSTEM_ERROR, "disk full"), "System error: disk full");
    BOOST_CHECK_EQUAL(NotificationHub::ShutdownMessage(ShutdownReason::INDEX_INIT_FAILED, "txindex"), "txindex failed to initialize");
}

BOOST_AUTO_TEST_CASE(the_dialog_shows_the_user_message_of_a_fatal_shutdown)
{
    std::vector<std::string> shown;
    const boost::signals2::scoped_connection box{uiInterface.ThreadSafeMessageBox.connect(
        [&shown](const std::string& message, const std::string& caption, unsigned int style) { shown.push_back(message); return false; })};
    FatalErrorDialog::Instance().ReportShutdown(ShutdownReason::DISK_SPACE_LOW, "");
    FatalErrorDialog::Instance().ReportShutdown(ShutdownReason::COINS_READ_FAILED, "bad key");
    FatalErrorDialog::Instance().ReportShutdown(ShutdownReason::BLOCK_WRITE_FAILED, "");
    const std::vector<std::string> expected{
        "Error: Disk space is low!",
        "Error reading from database, shutting down.",
        "Error: A fatal internal error occurred, see debug.log for details"};
    BOOST_CHECK_EQUAL_COLLECTIONS(shown.begin(), shown.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(upgrade_progress_reaches_the_gui)
{
    std::vector<std::tuple<std::string, int, bool>> shown;
    const boost::signals2::scoped_connection progress{uiInterface.ShowProgress.connect(
        [&shown](const std::string& title, int percent, bool resume_possible) { shown.emplace_back(title, percent, resume_possible); })};
    NotificationHub notifications;
    notifications.ReportUpgradeProgress(0);
    notifications.ReportUpgradeProgress(42);
    notifications.ReportUpgradeProgress(100);
    const std::vector<std::tuple<std::string, int, bool>> expected{
        {"Upgrading txindex database", 0, true}, {"Upgrading txindex database", 42, true}, {"", 100, false}};
    BOOST_CHECK(shown == expected);
}

// Negative cases: what must not show, change or happen.

BOOST_AUTO_TEST_CASE(reaching_the_stop_height_is_not_an_error)
{
    std::vector<std::string> log;
    Recorder recorder{"r", log};
    NotificationHub notifications;
    notifications.Subscribe(recorder);
    int changes{0};
    const boost::signals2::scoped_connection counter{uiInterface.NotifyAlertChanged.connect([&changes] { ++changes; })};
    int boxes{0};
    const boost::signals2::scoped_connection box{uiInterface.ThreadSafeMessageBox.connect(
        [&boxes](const std::string& message, const std::string& caption, unsigned int style) { ++boxes; return false; })};

    notifications.ReportShutdown(ShutdownReason::STOP_HEIGHT_REACHED, "100");
    FatalErrorDialog::Instance().ReportShutdown(ShutdownReason::STOP_HEIGHT_REACHED, "100");
    BOOST_CHECK_EQUAL(changes, 0);
    BOOST_CHECK_EQUAL(boxes, 0);
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(""));
    // The subscribers still hear it, so the node stops.
    const std::vector<std::string> expected{"r shutdown Stopping at height 100 as -stopatheight asks"};
    BOOST_CHECK_EQUAL_COLLECTIONS(log.begin(), log.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(resolving_without_a_fork_changes_nothing)
{
    NotificationHub notifications;
    int changes{0};
    const boost::signals2::scoped_connection counter{uiInterface.NotifyAlertChanged.connect([&changes] { ++changes; })};
    notifications.ReportForkResolved();
    BOOST_CHECK_EQUAL(changes, 0);
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(""));
}

BOOST_AUTO_TEST_CASE(the_same_fork_reported_again_tells_readers_nothing)
{
    NotificationHub notifications;
    int changes{0};
    const boost::signals2::scoped_connection counter{uiInterface.NotifyAlertChanged.connect([&changes] { ++changes; })};
    notifications.ReportLargeWorkFork(uint256S("01"));
    notifications.ReportLargeWorkFork(uint256S("01"));
    BOOST_CHECK_EQUAL(changes, 1);
}

BOOST_AUTO_TEST_CASE(upgrade_progress_sets_no_warning)
{
    NotificationHub notifications;
    int changes{0};
    const boost::signals2::scoped_connection counter{uiInterface.NotifyAlertChanged.connect([&changes] { ++changes; })};
    notifications.ReportUpgradeProgress(42);
    BOOST_CHECK_EQUAL(changes, 0);
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(""));
}

BOOST_AUTO_TEST_CASE(an_empty_store_shows_nothing)
{
    const NotificationHub notifications;
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), CLIENT_VERSION_IS_RELEASE ? "" : PRE_RELEASE);
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(""));
}

BOOST_AUTO_TEST_CASE(clearing_a_warning_that_is_not_set_changes_nothing)
{
    NotificationHub notifications;
    notifications.ReportClockOutOfSync(std::chrono::minutes{10});
    int changes{0};
    const boost::signals2::scoped_connection counter{uiInterface.NotifyAlertChanged.connect([&changes] { ++changes; })};

    notifications.ReportForkResolved();
    BOOST_CHECK_EQUAL(changes, 0);
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), ClockWarning());
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(ClockWarning()));
}

BOOST_AUTO_TEST_CASE(clearing_one_warning_keeps_the_others)
{
    NotificationHub notifications;
    notifications.ReportLargeWorkFork(uint256S("01"));
    notifications.ReportClockOutOfSync(std::chrono::minutes{10});
    notifications.ReportForkResolved();
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), ClockWarning());
    BOOST_CHECK_EQUAL(notifications.GetWarnings("gui"), GuiText(ClockWarning()));
}

BOOST_AUTO_TEST_CASE(a_clock_in_sync_clears_only_the_clock_warning)
{
    NotificationHub notifications;
    notifications.ReportShutdown(ShutdownReason::SYSTEM_ERROR, "disk full");
    int changes{0};
    const boost::signals2::scoped_connection counter{uiInterface.NotifyAlertChanged.connect([&changes] { ++changes; })};

    notifications.ReportClockInSync();
    BOOST_CHECK_EQUAL(changes, 0);
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), "System error: disk full");
}

// Subscribers: who hears what, and in which order.

BOOST_AUTO_TEST_CASE(every_subscriber_hears_each_publish_in_order)
{
    std::vector<std::string> log;
    Recorder first{"first", log};
    Recorder second{"second", log};
    NotificationHub notifications;
    notifications.Subscribe(first);
    notifications.Subscribe(second);

    notifications.ReportShutdown(ShutdownReason::SYSTEM_ERROR, "disk full");
    notifications.ReportClockOutOfSync(std::chrono::minutes{10});
    notifications.ReportClockInSync();
    notifications.ReportLargeWorkFork(uint256S("01"));
    notifications.ReportInvalidChainAhead();
    notifications.ReportForkResolved();
    notifications.ReportUpgradeProgress(42);

    const std::string fork{uint256S("01").ToString()};
    const std::vector<std::string> expected{
        "first shutdown System error: disk full", "second shutdown System error: disk full",
        "first clock 10", "second clock 10",
        "first clock ok", "second clock ok",
        "first fork " + fork, "second fork " + fork,
        "first invalid chain", "second invalid chain",
        "first fork resolved", "second fork resolved",
        "first upgrade 42", "second upgrade 42"};
    BOOST_CHECK_EQUAL_COLLECTIONS(log.begin(), log.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(subscribers_hear_a_shutdown_after_it_is_stored)
{
    std::vector<std::string> log;
    Recorder recorder{"r", log};
    NotificationHub notifications;
    notifications.Subscribe(recorder);
    notifications.ReportShutdown(ShutdownReason::SYSTEM_ERROR, "disk full");
    BOOST_CHECK_EQUAL(notifications.GetWarnings("statusbar"), "System error: disk full");
    const std::vector<std::string> expected{"r shutdown System error: disk full"};
    BOOST_CHECK_EQUAL_COLLECTIONS(log.begin(), log.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(an_unsubscribed_subscriber_hears_nothing)
{
    std::vector<std::string> log;
    Recorder kept{"kept", log};
    Recorder dropped{"dropped", log};
    NotificationHub notifications;
    notifications.Subscribe(kept);
    notifications.Subscribe(dropped);
    notifications.Unsubscribe(dropped);

    notifications.ReportShutdown(ShutdownReason::SYSTEM_ERROR, "disk full");
    const std::vector<std::string> expected{"kept shutdown System error: disk full"};
    BOOST_CHECK_EQUAL_COLLECTIONS(log.begin(), log.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(unsubscribing_a_stranger_changes_nothing)
{
    std::vector<std::string> log;
    Recorder subscribed{"subscribed", log};
    Recorder stranger{"stranger", log};
    NotificationHub notifications;
    notifications.Subscribe(subscribed);
    notifications.Unsubscribe(stranger);

    notifications.ReportInvalidChainAhead();
    const std::vector<std::string> expected{"subscribed invalid chain"};
    BOOST_CHECK_EQUAL_COLLECTIONS(log.begin(), log.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_SUITE_END()
