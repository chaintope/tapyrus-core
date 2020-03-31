// Copyright (c) 2018-2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <federationparams.h>
#include <script/sigcache.h>
#include <test/test_tapyrus.h>
#include <test/test_keys_helper.h>
#include <consensus/validation.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

extern void noui_connect();

struct FederationParamsTestingSetup {

    ECCVerifyHandle globalVerifyHandle;

    explicit FederationParamsTestingSetup(const std::string& chainName = TAPYRUS_MODES::PROD)
        : m_path_root(fs::temp_directory_path() / "test_tapyrus" / strprintf("%lu_%i", (unsigned long)GetTime(), (int)(InsecureRandRange(1 << 30))))
    {

        SHA256AutoDetect();
        RandomInit();
        ECC_Start();
        SetupEnvironment();
        SetupNetworking();
        InitSignatureCache();
        InitScriptExecutionCache();
        fCheckBlockIndex = true;
        SetDataDir("tempdir");
        writeTestGenesisBlockToFile(GetDataDir());
        noui_connect();
        SelectParams(TAPYRUS_OP_MODE::PROD);
    }

    ~FederationParamsTestingSetup()
    {
        ClearDatadirCache();
        gArgs.ClearOverrideArgs();
        fs::remove_all(m_path_root);
        ECC_Stop();
    }

    fs::path SetDataDir(const std::string& name)
    {
        fs::path ret = m_path_root / name;
        fs::create_directories(ret);
        gArgs.ForceSetArg("-datadir", ret.string());
        return ret;
    }

    fs::path GetDataDir()
    {
        return gArgs.GetArg("-datadir", "");
    }
private:
    const fs::path m_path_root;
};


BOOST_FIXTURE_TEST_SUITE(federationparams_tests, FederationParamsTestingSetup)


BOOST_AUTO_TEST_CASE(parse_pubkey_string_empty)
{
    BOOST_CHECK_NO_THROW(SelectFederationParams(TAPYRUS_OP_MODE::PROD));

    // When pubkey is not given.
    BOOST_CHECK_EXCEPTION(const_cast<CFederationParams&>(FederationParams()).ReadAggregatePubkey(ParseHex("")), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Aggregate Public Key for Signed Block is empty");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_uncompressed)
{
    BOOST_CHECK_NO_THROW(SelectFederationParams(TAPYRUS_OP_MODE::PROD));

    BOOST_CHECK_EXCEPTION(const_cast<CFederationParams&>(FederationParams()).ReadAggregatePubkey(ParseHex("046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd7")), std::runtime_error, [] (const std::runtime_error& ex) {
            BOOST_CHECK_EQUAL(ex.what(), "Uncompressed public key format are not acceptable: 046b93737b4e8d93e79464f2054434015326f1834be1ec47e23377a8cc622b94a03f3c58c0c33248e2bb733269751facb479c098eec6ce254e00c7e45c103b7cd7");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_invalid)
{
    // When too much pubkeys are given.
    BOOST_CHECK_NO_THROW(SelectFederationParams(TAPYRUS_OP_MODE::PROD));

    BOOST_CHECK_EXCEPTION(const_cast<CFederationParams&>(FederationParams()).ReadAggregatePubkey(ParseHex("03af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d02ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e0527b90002785a891f323acd6cef0fc509bb14304410595914267c50467e51c87142acbb5e02396c2c8a22ec28dbe02613027edea9a3b0c314294985e09c2f389818b29fee0603e67ceb1f0af0ab4668227984782b48d286b88e54dc91487143199728d4597c02023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e0205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a620201c537fd7eb7928700927b48e51ceec621fc8ba1177ee2ad67336ed91e2f63a1033e6e1d4ae3e7e1bc2173e2af1f2f65c6284ea7c6478f2241784c77b0dff98e6102114e7960286099c603e51348df63fd0acb75f81b97a85eb4af87df9ee5ff18eb03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b02b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a02e78cafe033b22bda5d7d1c8e82ee932930bf12e08489bc19769cbec765568be9")), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Aggregate Public Key for Signed Block is invalid: 03af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d02ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e0527b90002785a891f323acd6cef0fc509bb14304410595914267c50467e51c87142acbb5e02396c2c8a22ec28dbe02613027edea9a3b0c314294985e09c2f389818b29fee0603e67ceb1f0af0ab4668227984782b48d286b88e54dc91487143199728d4597c02023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e0205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a620201c537fd7eb7928700927b48e51ceec621fc8ba1177ee2ad67336ed91e2f63a1033e6e1d4ae3e7e1bc2173e2af1f2f65c6284ea7c6478f2241784c77b0dff98e6102114e7960286099c603e51348df63fd0acb75f81b97a85eb4af87df9ee5ff18eb03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b02b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a02e78cafe033b22bda5d7d1c8e82ee932930bf12e08489bc19769cbec765568be9");
        return true;
    });

    BOOST_CHECK_NO_THROW(Params());
}


BOOST_AUTO_TEST_CASE(create_genesis_block)
{
    BOOST_CHECK_NO_THROW(SelectFederationParams(TAPYRUS_OP_MODE::PROD));

    CKey key;
    key.Set(validAggPrivateKey, validAggPrivateKey + 32, true);

    CPubKey aggregatePubkey(validAggPubKey, validAggPubKey + 33);
    const auto genesis = createGenesisBlock(aggregatePubkey, key);

    CValidationState state;
    BOOST_CHECK(CheckBlock(genesis, state, true));
}

BOOST_AUTO_TEST_CASE(create_genesis_block_one_publickey)
{
    CKey aggregateKey;
    aggregateKey.Set(validAggPrivateKey, validAggPrivateKey + 32, true);
    CPubKey aggPubkey = aggregateKey.GetPubKey();

    auto baseChainParams = CreateFederationParams(TAPYRUS_OP_MODE::PROD, true);
    baseChainParams->ReadGenesisBlock(getTestGenesisBlockHex(aggPubkey, aggregateKey));

    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().vtx.size(), 1);
    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().nVersion, 1);
    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().hashPrevBlock.ToString(), "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().hashMerkleRoot, baseChainParams->GenesisBlock().vtx[0]->GetHash());
    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().hashImMerkleRoot, baseChainParams->GenesisBlock().vtx[0]->GetHashMalFix());

    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().vtx[0]->vin[0].prevout.hashMalFix.ToString(), "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().vtx[0]->vin[0].prevout.n, 0);

    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().vtx[0]->vin.size(), 1);
    CScript scriptSig = baseChainParams->GenesisBlock().vtx[0]->vin[0].scriptSig;
    BOOST_CHECK_EQUAL(HexStr(scriptSig.begin(), scriptSig.end()), "");

    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().vtx[0]->vout.size(), 1);
    BOOST_CHECK_EQUAL(baseChainParams->GenesisBlock().vtx[0]->vout[0].nValue, 50 * COIN);
    CScript scriptPubKey = baseChainParams->GenesisBlock().vtx[0]->vout[0].scriptPubKey;
    BOOST_CHECK_EQUAL(HexStr(scriptPubKey.begin(), scriptPubKey.end()),
    "76a914834e0737cdb9008db614cd95ec98824e952e3dc588ac");

    //BOOST_CHECK_EQUAL(chainParams->GenesisBlock().GetHash(), chainParams->GetConsensus().hashGenesisBlock);

    //verify signature
    const uint256 blockHash = baseChainParams->GenesisBlock().GetHashForSign();

    BOOST_CHECK(aggPubkey.Verify_Schnorr(blockHash, baseChainParams->GenesisBlock().proof));
}

BOOST_AUTO_TEST_SUITE_END()
