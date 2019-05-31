// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <util.h>
#include <test/test_bitcoin.h>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainparamsbase_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(parse_chain_params_base_options_parameters)
{
    char const* argv[5] = {"bitcoind", "-regtest", "-signblockpubkeys=021a564bd5d483d1f248e15d25d8a77e7a0993080e9ecd1a254cb6f6b2515a1fc0", "-signblockthreshold=1", "-dummy=abc"};

    SetupChainParamsBaseOptions();
    std::string error;

    BOOST_CHECK(ParseSignedBlockParameters(5, argv, error));

    BOOST_CHECK_EQUAL(gArgs.GetChainName(), CBaseChainParams::MAIN);//argument is ignored
    BOOST_CHECK_EQUAL(gArgs.GetArg("-signblockpubkeys", ""), "021a564bd5d483d1f248e15d25d8a77e7a0993080e9ecd1a254cb6f6b2515a1fc0");
    BOOST_CHECK_EQUAL(gArgs.GetArg("-signblockthreshold", 0), 1);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-dummy", ""), "");
}

BOOST_AUTO_TEST_SUITE_END()