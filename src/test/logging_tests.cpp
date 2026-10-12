// Copyright (c) 2019-2022 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <debuglogfile.h>
#include <logging.h>
#include <test/test_tapyrus.h>
#include <threadnames.h>
#include <util.h>

#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {

//! A logger that keeps the lines it would write to a file.
class CaptureLogger : public BCLog::Logger
{
public:
    std::vector<std::string> m_lines;

    CaptureLogger() { m_log_timestamps = false; }
    bool Enabled() const override { return true; }

protected:
    void WriteLine(const std::string& line) override { m_lines.push_back(line); }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(logging_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(logging_escape_message)
{
    BOOST_CHECK_EQUAL(BCLog::LogEscapeMessage("foo"), "foo");
    BOOST_CHECK_EQUAL(BCLog::LogEscapeMessage("foo\nbar"), "foo\nbar");
    BOOST_CHECK_EQUAL(BCLog::LogEscapeMessage("foo\r\nbar"), "foo\\x0d\nbar");
    BOOST_CHECK_EQUAL(BCLog::LogEscapeMessage("foo\x01\x02\x7f"), "foo\\x01\\x02\\x7f");
    BOOST_CHECK_EQUAL(BCLog::LogEscapeMessage("\x1b[31mred"), "\\x1b[31mred");
    // UTF-8 is not a control sequence and is kept as is.
    BOOST_CHECK_EQUAL(BCLog::LogEscapeMessage("\xc3\xa5"), "\xc3\xa5");
}

BOOST_AUTO_TEST_CASE(logging_prefixes)
{
    CaptureLogger logger;
    const SourceLocation loc{__func__};
    logger.LogPrintStr("msg", loc, BCLog::NET, BCLog::Level::Debug);
    logger.LogPrintStr("msg", loc, BCLog::NET, BCLog::Level::Info);
    logger.LogPrintStr("msg", loc, BCLog::ALL, BCLog::Level::Info);
    logger.LogPrintStr("msg", loc, BCLog::ALL, BCLog::Level::Warning);
    logger.LogPrintStr("msg\n", loc, BCLog::ALL, BCLog::Level::Error);
    logger.LogPrintStr("a\rb", loc, BCLog::ALL, BCLog::Level::Info);
    logger.m_always_print_category_level = true;
    logger.LogPrintStr("msg", loc, BCLog::NET, BCLog::Level::Debug);
    logger.LogPrintStr("msg", loc, BCLog::ALL, BCLog::Level::Info);

    const std::vector<std::string> expected{
        "[net] msg\n",
        "[net:info] msg\n",
        "msg\n",
        "[warning] msg\n",
        "[error] msg\n",
        "a\\x0db\n",
        "[net:debug] msg\n",
        "[all:info] msg\n",
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(logger.m_lines.begin(), logger.m_lines.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(logging_legacy_output_unchanged)
{
    // LogPrintf output keeps the old format: no prefix, no added newline, and a
    // line built from several calls is timestamped once.
    CaptureLogger logger;
    logger.m_log_timestamps = true;
    const SourceLocation loc{__func__};
    logger.LogPrintLegacyStr("first ", loc);
    logger.LogPrintLegacyStr("second\n", loc);
    logger.LogPrintLegacyStr("third\n", loc);

    // Each new line starts with the timestamp (mocktime may add to it) and nothing else.
    BOOST_REQUIRE_EQUAL(logger.m_lines.size(), 3U);
    BOOST_CHECK(logger.m_lines[0].ends_with(" first ") && logger.m_lines[0] != " first ");
    BOOST_CHECK_EQUAL(logger.m_lines[1], "second\n");
    BOOST_CHECK(logger.m_lines[2].ends_with(" third\n") && logger.m_lines[2] != " third\n");
    BOOST_CHECK(logger.m_lines[2].find('[') == std::string::npos);
}

BOOST_AUTO_TEST_CASE(logging_thread_and_source_location)
{
    CaptureLogger logger;
    logger.m_log_threadnames = true;
    logger.m_log_sourcelocations = true;

    std::thread thread([&] {
        ThreadSetInternalName("logtest");
        logger.LogPrintStr("msg", SourceLocation{__func__}, BCLog::ALL, BCLog::Level::Info);
    });
    thread.join();

    BOOST_REQUIRE_EQUAL(logger.m_lines.size(), 1U);
    const std::string& line{logger.m_lines[0]};
    BOOST_CHECK(line.starts_with("[logtest] ["));
    BOOST_CHECK(line.find("logging_tests.cpp:") != std::string::npos);
    BOOST_CHECK(line.ends_with("] msg\n"));
}

BOOST_AUTO_TEST_CASE(logging_thread_names)
{
    std::string internal_name;
    std::thread thread([&] {
        RenameThread("tapyrus-logtest");
        internal_name = ThreadGetInternalName();
    });
    thread.join();
    BOOST_CHECK_EQUAL(internal_name, "tapyrus-logtest");

    std::string unnamed{"not set"};
    std::thread unnamed_thread([&] { unnamed = ThreadGetInternalName(); });
    unnamed_thread.join();
    BOOST_CHECK_EQUAL(unnamed, "");
}

BOOST_AUTO_TEST_CASE(logging_levels)
{
    CaptureLogger logger;
    logger.EnableCategory(BCLog::NET);

    // Info and above are always logged, whatever the category.
    BOOST_CHECK(logger.WillLogCategoryLevel(BCLog::MEMPOOL, BCLog::Level::Info));
    BOOST_CHECK(logger.WillLogCategoryLevel(BCLog::MEMPOOL, BCLog::Level::Error));
    BOOST_CHECK(!logger.WillLogCategoryLevel(BCLog::MEMPOOL, BCLog::Level::Debug));

    // The default level is Debug.
    BOOST_CHECK(logger.WillLogCategoryLevel(BCLog::NET, BCLog::Level::Debug));
    BOOST_CHECK(!logger.WillLogCategoryLevel(BCLog::NET, BCLog::Level::Trace));

    BOOST_CHECK(logger.SetLogLevel("trace"));
    BOOST_CHECK(logger.WillLogCategoryLevel(BCLog::NET, BCLog::Level::Trace));

    // Only info, debug and trace can be chosen.
    BOOST_CHECK(!logger.SetLogLevel("warning"));
    BOOST_CHECK(!logger.SetLogLevel("bogus"));
    BOOST_CHECK(logger.LogLevel() == BCLog::Level::Trace);

    // A category level overrides the global one.
    BOOST_CHECK(logger.SetCategoryLogLevel("net", "info"));
    BOOST_CHECK(!logger.WillLogCategoryLevel(BCLog::NET, BCLog::Level::Debug));
    BOOST_CHECK(logger.WillLogCategoryLevel(BCLog::NET, BCLog::Level::Info));
    BOOST_CHECK(!logger.SetCategoryLogLevel("bogus", "info"));
    BOOST_CHECK(!logger.SetCategoryLogLevel("net", "error"));

    BOOST_CHECK_EQUAL(BCLog::Logger::LogLevelsString(), "info, debug, trace");
}

BOOST_AUTO_TEST_CASE(debug_log_file_early_buffer_limit)
{
    const fs::path path{fs::temp_directory_path() / strprintf("tapyrus_logging_test_%d.log", GetTime())};
    fs::remove(path);
    {
        DebugLogFile log_file(path, /*max_buffer_memusage=*/size_t{500});
        log_file.m_log_timestamps = false;
        const int lines{10};
        for (int i = 0; i < lines; ++i) {
            log_file.LogPrintStr(strprintf("line %d %s", i, std::string(80, 'x')), SourceLocation{__func__}, BCLog::ALL, BCLog::Level::Info);
        }
        BOOST_REQUIRE(log_file.Open());
    }

    std::ifstream file{path.string()};
    std::vector<std::string> written;
    for (std::string line; std::getline(file, line);) written.push_back(line);
    fs::remove(path);

    // The oldest lines were dropped; the rest follow in order, then the count of dropped lines.
    BOOST_REQUIRE_GE(written.size(), 2U);
    const size_t kept{written.size() - 1};
    BOOST_CHECK_LT(kept, 10U);
    BOOST_CHECK(written.front().starts_with(strprintf("line %d ", 10 - kept)));
    BOOST_CHECK(written[kept - 1].starts_with("line 9 "));
    BOOST_CHECK_EQUAL(written.back(), strprintf("Early logging buffer overflowed, %d log lines discarded.", 10 - kept));
}

BOOST_AUTO_TEST_SUITE_END()
