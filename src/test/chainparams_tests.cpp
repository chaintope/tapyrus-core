//
// Created by taniguchi on 2018/11/27.
//

#include <chainparams.h>
#include <chainparams.cpp>
#include <test/test_bitcoin.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainparams_tests, BasicTestingSetup)

std::vector<CPubKey> validPubkeys(int n)
{
    std::vector<CPubKey> pubkeys;
    for (int i = 0; i < n; i++) {
        CKey key;
        key.MakeNewKey(true);
        pubkeys.push_back(key.GetPubKey());
    }

    return pubkeys;
}

std::string combinedPubkeyString(std::vector<CPubKey> pubkeys)
{
    std::string r;
    for (CPubKey pubkey : pubkeys) {
        r += HexStr(pubkey.begin(), pubkey.end());
    }
    return r;
}

bool correctNumberOfKeysErrorMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("Public Keys for Signed Block are up to 15, but passed 16 keys."));
    return true;
}

bool correctIncludeInvalidPubKeyMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), "Public Keys for Signed Block include invalid pubkey: hoge");
    return true;
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_valid_15_keys)
{
    std::string str = combinedPubkeyString(validPubkeys(15));
    std::vector<CPubKey> pubkeys = ParsePubkeyString(str);

    BOOST_CHECK_EQUAL(pubkeys.size(), 15);

    for(unsigned int i = 1; i < pubkeys.size(); i++) {
        BOOST_CHECK(pubkeys.at(i - 1) < pubkeys.at(i));
    }
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_public_keys_that_include_uncompressed)
{
    CKey key;
    key.MakeNewKey(false); // create uncompressed pubkey
    std::vector<CPubKey> pubkeys = validPubkeys(14);
    pubkeys.insert(pubkeys.begin(), key.GetPubKey());
    std::string str = combinedPubkeyString(pubkeys);
    BOOST_CHECK_EQUAL(ParsePubkeyString(str).size(), 15);
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_walid_over_15_keys)
{
    std::string str = combinedPubkeyString(validPubkeys(16));
    BOOST_CHECK_EXCEPTION(ParsePubkeyString(str), std::runtime_error, correctNumberOfKeysErrorMessage);
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_keys_include_invalid)
{
    std::string str = "hoge";
    BOOST_CHECK_EXCEPTION(ParsePubkeyString(str), std::runtime_error, correctIncludeInvalidPubKeyMessage);
}

BOOST_AUTO_TEST_CASE(create_cchainparams_instance)
{
    gArgs.SoftSetArg("-signblockpubkeys", combinedPubkeyString(validPubkeys(15)));
    gArgs.SoftSetArg("-signblockthreshold", "10");
    std::unique_ptr<CChainParams> params = CreateChainParams(CBaseChainParams::MAIN);

    BOOST_CHECK_EQUAL(params->GetSignedBlockCondition().pubkeys.size(), 15);
    BOOST_CHECK_EQUAL(params->GetSignedBlockCondition().threshold, 10);
}



BOOST_AUTO_TEST_SUITE_END()