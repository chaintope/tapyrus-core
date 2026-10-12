// Copyright (c) 2019-2022 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <debuglogfile.h>
#include <logging.h>
#include <test/test_tapyrus.h>

#include <fstream>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

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

BOOST_AUTO_TEST_CASE(debug_log_file_early_buffer_limit)
{
    const fs::path path{fs::temp_directory_path() / strprintf("tapyrus_logging_test_%d.log", GetTime())};
    fs::remove(path);
    {
        DebugLogFile log_file(path, /*max_buffer_memusage=*/size_t{500});
        log_file.m_log_timestamps = false;
        const int lines{10};
        for (int i = 0; i < lines; ++i) {
            log_file.LogPrintStr(strprintf("line %d %s\n", i, std::string(80, 'x')));
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
