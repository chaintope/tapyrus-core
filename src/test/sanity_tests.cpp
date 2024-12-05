// Copyright (c) 2012-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <test/test_tapyrus.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(sanity_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(basic_sanity)
{
  BOOST_CHECK_MESSAGE(ECC_InitSanityCheck() == true, "secp256k1 sanity test");
}

BOOST_AUTO_TEST_SUITE_END()
