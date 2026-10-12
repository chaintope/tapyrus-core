// Copyright (c) 2019-2022 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include <test/test_tapyrus.h>

#include <string>

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

BOOST_AUTO_TEST_SUITE_END()
