// Copyright (c) 2014-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensussettings.h>
#include <federationparams.h>
#include <genesisfile.h>
#include <validation.h>
#include <connman.h>

#include <test/test_tapyrus.h>

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

/** Settings with a halving interval that neither mode has. */
class HalvingIntervalSettings : public ConsensusSettings
{
public:
    explicit HalvingIntervalSettings(int32_t subsidyHalvingInterval)
        : ConsensusSettings(ReadGenesisBlock(), TAPYRUS_OP_MODE::PROD, FederationParams().NetworkId(), subsidyHalvingInterval) {}
};

static void TestBlockSubsidyHalvings(const ConsensusSettings& settings)
{
    int maxHalvings = 64;
    CAmount nInitialSubsidy = 50 * ConsensusSettings::COIN;

    CAmount nPreviousSubsidy = nInitialSubsidy * 2; // for height == 0
    BOOST_CHECK_EQUAL(nPreviousSubsidy, nInitialSubsidy * 2);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        int nHeight = nHalvings * settings.SubsidyHalvingInterval();
        CAmount nSubsidy = settings.BlockSubsidy(nHeight);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nSubsidy;
    }
    BOOST_CHECK_EQUAL(settings.BlockSubsidy(maxHalvings * settings.SubsidyHalvingInterval()), 0);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    TestBlockSubsidyHalvings(*g_consensus_settings); // As in prod
    TestBlockSubsidyHalvings(ConsensusSettings(ReadGenesisBlock(), TAPYRUS_OP_MODE::DEV, FederationParams().NetworkId())); // As in dev
    TestBlockSubsidyHalvings(HalvingIntervalSettings(1000)); // Just another interval
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    CAmount nSum = 0;
    for (int nHeight = 0; nHeight < 14000000; nHeight += 1000) {
        CAmount nSubsidy = g_consensus_settings->BlockSubsidy(nHeight);
        BOOST_CHECK(nSubsidy <= 50 * ConsensusSettings::COIN);
        nSum += nSubsidy * 1000;
        BOOST_CHECK(MoneyRange(nSum));
    }
    BOOST_CHECK_EQUAL(nSum, CAmount{2099999997690000});
}

static bool ReturnFalse() { return false; }
static bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool (), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}
BOOST_AUTO_TEST_SUITE_END()
