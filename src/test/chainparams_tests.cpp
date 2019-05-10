//
// Created by taniguchi on 2018/11/27.
//

#include <chainparams.h>
#include <chainparams.cpp>
#include <test/test_bitcoin.h>
#include <test/test_keys_helper.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainparams_tests, BasicTestingSetup)

bool correctNumberOfKeysErrorMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), std::string("Public Keys for Signed Block are up to 15, but passed 16 keys."));
    return true;
}

bool includeUncompressedKeysErrorMessage(const std::runtime_error& ex)
{
    BOOST_CHECK_EQUAL(ex.what(), "Uncompressed public key format are not acceptable: 046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd703af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d02ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e0527b90002785a891f323acd6cef0fc509bb14304410595914267c50467e51c87142acbb5e02396c2c8a22ec28dbe02613027edea9a3b0c314294985e09c2f389818b29fee0603e67ceb1f0af0ab4668227984782b48d286b88e54dc91487143199728d4597c02023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e0205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a620201c537fd7eb7928700927b48e51ceec621fc8ba1177ee2ad67336ed91e2f63a1033e6e1d4ae3e7e1bc2173e2af1f2f65c6284ea7c6478f2241784c77b0dff98e6102114e7960286099c603e51348df63fd0acb75f81b97a85eb4af87df9ee5ff18eb03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b02b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a");
    return true;
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_valid_15_keys)
{
    std::string str = combinedPubkeyString(15);
    std::vector<CPubKey> pubkeys = ParsePubkeyString(str);

    BOOST_CHECK_EQUAL(pubkeys.size(), 15);

    for(unsigned int i = 1; i < pubkeys.size(); i++) {
        BOOST_CHECK(pubkeys.at(i - 1) < pubkeys.at(i));
    }
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_public_keys_that_include_uncompressed)
{
    std::string str = UncompressedPubKeyString + combinedPubkeyString(14);
    BOOST_CHECK_EXCEPTION(ParsePubkeyString(str), std::runtime_error, includeUncompressedKeysErrorMessage);
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_keys_include_invalid)
{
    std::string str = combinedPubkeyString(14) + "030000000000000000000000000000000000000000000000000000000000000005";
    BOOST_CHECK_EXCEPTION(ParsePubkeyString(str), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Public Keys for Signed Block include invalid pubkey: 030000000000000000000000000000000000000000000000000000000000000005");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(create_cchainparams_instance)
{
    std::unique_ptr<CChainParams> params;

    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "10");
    params = CreateChainParams(CBaseChainParams::MAIN);

    BOOST_CHECK_EQUAL(params->GetSignedBlocksCondition().pubkeys.size(), 15);
    BOOST_CHECK_EQUAL(params->GetSignedBlocksCondition().threshold, 10);

    // When pubkey is not given.
    gArgs.ForceSetArg("-signblockpubkeys", "");
    gArgs.ForceSetArg("-signblockthreshold", "10");

    BOOST_CHECK_EXCEPTION(CreateChainParams(CBaseChainParams::MAIN), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Threshold can be between 1 to 0, but passed 10.");
        return true;
    });

    // When too much pubkeys are given.
    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(16));
    gArgs.ForceSetArg("-signblockthreshold", "10");

    BOOST_CHECK_EXCEPTION(CreateChainParams(CBaseChainParams::MAIN), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Public Keys for Signed Block are up to 15, but passed 16 keys.");
        return true;
    });

    // When too much pubkeys are given.
    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "0");

    BOOST_CHECK_EXCEPTION(CreateChainParams(CBaseChainParams::MAIN), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Threshold can be between 1 to 15, but passed 0.");
        return true;
    });

    // When too much pubkeys are given.
    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "16");

    BOOST_CHECK_EXCEPTION(CreateChainParams(CBaseChainParams::MAIN), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Threshold can be between 1 to 15, but passed 16.");
        return true;
    });

    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "10");
}

BOOST_AUTO_TEST_CASE(create_genesis_block)
{
    std::vector<unsigned char> vch = ParseHex("0296da90ddaedb8ca76561fc5660c40be68c72415d89e91ed3de73720028533840");
    CPubKey rewardTo(vch.begin(), vch.end());

    MultisigCondition condition = CreateSignedBlocksCondition(combinedPubkeyString(15), 10);
    CBlock genesis = CreateGenesisBlock(1546853016, 1, 50 * COIN, HexStr(rewardTo.begin(), rewardTo.end()), condition);

    CScript scriptSig = genesis.vtx[0].get()->vin[0].scriptSig;
    BOOST_CHECK_EQUAL(HexStr(scriptSig.begin(), scriptSig.end()), "010a2102d7bbe714a08f73b17a3e5dcbca523470e9de5ee6c92f396beb954b8a2cdf4388");

    CScript scriptPubKey = genesis.vtx[0].get()->vout[0].scriptPubKey;
    BOOST_CHECK_EQUAL(HexStr(scriptPubKey.begin(), scriptPubKey.end()), "76a914900a91031a3eb3f9a3ce08f866444227689ad3c588ac");
}

BOOST_AUTO_TEST_SUITE_END()
//
//error: in "chainparams_tests/parse_pubkey_string_when_passed_public_keys_that_include_uncompressed":
//check ex.what() == "Uncompressed public key format are not acceptable: 046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd7021a564bd5d483d1f248e15d25d8a77e7a0993080e9ecd1a254cb6f6b2515a1fc0020ad770bc838f167b9e0dc8781ff5dea1cc5c175ec8fc5af355855efaf69e8760028d14924263f705b0142f21d6a9673ec8e3a30704bb502c5c3ab5a019cd0b624d03fa7ff860dd5b24a0d988d7b5bae84f246296d393d690c89dd2625be9908061e402958a0b98998310637bb7a032fb4d97234e5a38e01c07ea7b7a541c25e907b387033ac73090f32a210c40d72a685a611f0331d5f2f78d05dea1b4faa59fa5f909e70393277249809d8f9397bebd191a169ad4e46c9a5d39f2ef2653d1ba8dc682b54a02e9698347edd21006af11da7384eb44a734e6631dbcbe20d1ea52dd0f76caa9e303fcccf899c66027dc0fb39bc2b60a8ecf00156a79106c12eeb557280b53b76220026bda2fe2ff85358b57a3fb5f5dd9ba749ea3851e30607a692a4580623963d7b403b90e054989ea73c12fbf9d1ff2dd0cc4cb027e4893acf78657e36cba19ec241502aaac0f82c3f14ed46736eb01a5d372ca96540269680fc1e200da4e3f5f704c0f02eb6858400391f8d9f169f6de56204ea8467926604e709e1f35c81bbcf3cfff9003a89ac28fdda61eb9e2100a26b2fa2e37bddb4967d45d9869853557e91627075d"
//has failed [Uncompressed public key format are not acceptable: 046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd703af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d02ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e0527b90002785a891f323acd6cef0fc509bb14304410595914267c50467e51c87142acbb5e02396c2c8a22ec28dbe02613027edea9a3b0c314294985e09c2f389818b29fee0603e67ceb1f0af0ab4668227984782b48d286b88e54dc91487143199728d4597c02023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e0205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a620201c537fd7eb7928700927b48e51ceec621fc8ba1177ee2ad67336ed91e2f63a1033e6e1d4ae3e7e1bc2173e2af1f2f65c6284ea7c6478f2241784c77b0dff98e6102114e7960286099c603e51348df63fd0acb75f81b97a85eb4af87df9ee5ff18eb03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b02b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a
//    !=
//            Uncompressed public key format are not acceptable:
//    046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd7021a564bd5d483d1f248e15d25d8a77e7a0993080e9ecd1a254cb6f6b2515a1fc0020ad770bc838f167b9e0dc8781ff5dea1cc5c175ec8fc5af355855efaf69e8760028d14924263f705b0142f21d6a9673ec8e3a30704bb502c5c3ab5a019cd0b624d03fa7ff860dd5b24a0d988d7b5bae84f246296d393d690c89dd2625be9908061e402958a0b98998310637bb7a032fb4d97234e5a38e01c07ea7b7a541c25e907b387033ac73090f32a210c40d72a685a611f0331d5f2f78d05dea1b4faa59fa5f909e70393277249809d8f9397bebd191a169ad4e46c9a5d39f2ef2653d1ba8dc682b54a02e9698347edd21006af11da7384eb44a734e6631dbcbe20d1ea52dd0f76caa9e303fcccf899c66027dc0fb39bc2b60a8ecf00156a79106c12eeb557280b53b76220026bda2fe2ff85358b57a3fb5f5dd9ba749ea3851e30607a692a4580623963d7b403b90e054989ea73c12fbf9d1ff2dd0cc4cb027e4893acf78657e36cba19ec241502aaac0f82c3f14ed46736eb01a5d372ca96540269680fc1e200da4e3f5f704c0f02eb6858400391f8d9f169f6de56204ea8467926604e709e1f35c81bbcf3cfff9003a89ac28fdda61eb9e2100a26b2fa2e37bddb4967d45d9869853557e91627075d]
