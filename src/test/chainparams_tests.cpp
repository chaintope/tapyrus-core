// Copyright (c) 2018-2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <crypto/sha256.h>
#include <validation.h>
#include <script/sigcache.h>
#include <test/test_tapyrus.h>
#include <test/test_keys_helper.h>

#include <boost/test/unit_test.hpp>
#include <consensus/validation.h>


BOOST_FIXTURE_TEST_SUITE(chainparams_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(parse_pubkey_string_uncompressed)
{
    BOOST_CHECK_NO_THROW(SelectParams(CBaseChainParams::MAIN));

    BOOST_CHECK_EXCEPTION(const_cast<CChainParams&>(Params()).ReadAggregatePubkey(ParseHex("046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd7")), std::runtime_error, [] (const std::runtime_error& ex) {
            BOOST_CHECK_EQUAL(ex.what(), "Uncompressed public key format are not acceptable: 046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd7");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(create_cchainparams_gargs_highthreshold)
{
    // When too much pubkeys are given.
    BOOST_CHECK_NO_THROW(SelectParams(CBaseChainParams::MAIN));

    BOOST_CHECK_EXCEPTION(const_cast<CChainParams&>(Params()).ReadAggregatePubkey(ParseHex("03af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d02ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e0527b90002785a891f323acd6cef0fc509bb14304410595914267c50467e51c87142acbb5e02396c2c8a22ec28dbe02613027edea9a3b0c314294985e09c2f389818b29fee0603e67ceb1f0af0ab4668227984782b48d286b88e54dc91487143199728d4597c02023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e0205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a620201c537fd7eb7928700927b48e51ceec621fc8ba1177ee2ad67336ed91e2f63a1033e6e1d4ae3e7e1bc2173e2af1f2f65c6284ea7c6478f2241784c77b0dff98e6102114e7960286099c603e51348df63fd0acb75f81b97a85eb4af87df9ee5ff18eb03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b02b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a02e78cafe033b22bda5d7d1c8e82ee932930bf12e08489bc19769cbec765568be9")), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Aggregate Public Key for Signed Block is invalid: 03af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d02ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e0527b90002785a891f323acd6cef0fc509bb14304410595914267c50467e51c87142acbb5e02396c2c8a22ec28dbe02613027edea9a3b0c314294985e09c2f389818b29fee0603e67ceb1f0af0ab4668227984782b48d286b88e54dc91487143199728d4597c02023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e0205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a620201c537fd7eb7928700927b48e51ceec621fc8ba1177ee2ad67336ed91e2f63a1033e6e1d4ae3e7e1bc2173e2af1f2f65c6284ea7c6478f2241784c77b0dff98e6102114e7960286099c603e51348df63fd0acb75f81b97a85eb4af87df9ee5ff18eb03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b02b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a02e78cafe033b22bda5d7d1c8e82ee932930bf12e08489bc19769cbec765568be9");
        return true;
    });

    BOOST_CHECK_NO_THROW(Params());
}

BOOST_AUTO_TEST_CASE(create_cchainparams_empty)
{
    BOOST_CHECK_NO_THROW(SelectParams(CBaseChainParams::MAIN));

    // When pubkey is not given.
    BOOST_CHECK_EXCEPTION(const_cast<CChainParams&>(Params()).ReadAggregatePubkey(ParseHex("")), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Aggregate Public Key for Signed Block is empty");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(create_genesis_block)
{
    // This is for using CPubKey.verify().
    ECCVerifyHandle globalVerifyHandle;

    CKey key;
    key.Set(validAggPrivateKey, validAggPrivateKey + 32, true);

    CPubKey aggregatePubkey(validAggPubKey, validAggPubKey + 33);
    const auto genesis = createGenesisBlock(aggregatePubkey, key);

    CValidationState state;
    BOOST_CHECK(CheckBlock(genesis, state, Params().GetConsensus(), true));
}


BOOST_AUTO_TEST_SUITE_END()
