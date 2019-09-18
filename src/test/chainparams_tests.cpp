//
// Created by taniguchi on 2018/11/27.
//

#include <chainparams.h>
#include <crypto/sha256.h>
#include <validation.h>
#include <script/sigcache.h>
#include <test/test_tapyrus.h>
#include <test/test_keys_helper.h>

#include <boost/test/unit_test.hpp>
#include <consensus/validation.h>

/** ChainParamsTestingSetup
 * This class is an exact copy of BasicTestingSetup but removes two steps:
 *   * MultisigCondition
 *   * SelectParams
 * this is needed to test the exceptions raised by the constructor of MultisigCondition
 * Note: do not use this class in any other unit test
 */
extern void noui_connect();
struct ChainParamsTestingSetup{
    explicit ChainParamsTestingSetup(const std::string& chainName = CBaseChainParams::MAIN)
    : m_path_root(fs::temp_directory_path() / "test_bitcoin" / strprintf("%lu_%i", (unsigned long)GetTime(), (int)(InsecureRandRange(1 << 30))))
    {
        SHA256AutoDetect();
        RandomInit();
        ECC_Start();
        SetupEnvironment();
        SetupNetworking();
        InitSignatureCache();
        InitScriptExecutionCache();
        fCheckBlockIndex = true;
        noui_connect();
    }

    ~ChainParamsTestingSetup()
    {
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
    private:
    const fs::path m_path_root;
};


BOOST_FIXTURE_TEST_SUITE(chainparams_tests, ChainParamsTestingSetup)

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
    //ParsePubkeyString is called by the constructor internally
    MultisigCondition signedBlockCondition(str, 10);

    //using local object
    BOOST_CHECK_EQUAL(signedBlockCondition.pubkeys.size(), 15);
    BOOST_CHECK_EQUAL(signedBlockCondition.threshold, 10);

    for(unsigned int i = 1; i < signedBlockCondition.pubkeys.size(); i++) {
        BOOST_CHECK(signedBlockCondition.pubkeys.at(i - 1) < signedBlockCondition.pubkeys.at(i));
    }
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_public_keys_that_include_uncompressed)
{
    std::string str = UncompressedPubKeyString + combinedPubkeyString(14);
    MultisigCondition signedBlockCondition("02473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b", 1);
    BOOST_CHECK_EXCEPTION(signedBlockCondition.ParsePubkeyString(str), std::runtime_error, includeUncompressedKeysErrorMessage);
}

BOOST_AUTO_TEST_CASE(parse_pubkey_string_when_passed_keys_include_invalid)
{
    std::string str = combinedPubkeyString(14) + "030000000000000000000000000000000000000000000000000000000000000005";
    MultisigCondition signedBlockCondition("02473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b", 1);
    BOOST_CHECK_EXCEPTION(signedBlockCondition.ParsePubkeyString(str), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Public Keys for Signed Block include invalid pubkey: 030000000000000000000000000000000000000000000000000000000000000005");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(create_cchainparams_gargs)
{
    SelectParams(CBaseChainParams::MAIN);
    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "10");

    SetSignedBlocksCondition();

    BOOST_CHECK_EQUAL(Params().GetSignedBlocksCondition().pubkeys.size(), 15);
    BOOST_CHECK_EQUAL(Params().GetSignedBlocksCondition().threshold, 10);

    // When pubkey is not given.
    gArgs.ForceSetArg("-signblockpubkeys", "");
    gArgs.ForceSetArg("-signblockthreshold", "10");

    //signedblock condition is initialized only once
    BOOST_CHECK_NO_THROW(CreateChainParams(CBaseChainParams::MAIN));
    BOOST_CHECK_NO_THROW(CreateChainParams(CBaseChainParams::TESTNET));
}

BOOST_AUTO_TEST_CASE(create_cchainparams_gargs_toomany)
{
    SelectParams(CBaseChainParams::MAIN);
    // When too much pubkeys are given.
    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(16));
    gArgs.ForceSetArg("-signblockthreshold", "10");

    BOOST_CHECK_EXCEPTION(SetSignedBlocksCondition(), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Public Keys for Signed Block are up to 15, but passed 16.");
        return true;
    });

    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    BOOST_CHECK_NO_THROW(SetSignedBlocksCondition());
}

BOOST_AUTO_TEST_CASE(create_cchainparams_gargs_lowthreshold)
{
    SelectParams(CBaseChainParams::MAIN);
    // When too much pubkeys are given.
    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "0");

    BOOST_CHECK_EXCEPTION(SetSignedBlocksCondition(), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Threshold can be between 1 to 15, but passed 0.");
        return true;
    });

    gArgs.ForceSetArg("-signblockthreshold", "10");
    BOOST_CHECK_NO_THROW(SetSignedBlocksCondition());
}

BOOST_AUTO_TEST_CASE(create_cchainparams_gargs_highthreshold)
{
    SelectParams(CBaseChainParams::MAIN);
    // When too much pubkeys are given.
    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "16");

    BOOST_CHECK_EXCEPTION(SetSignedBlocksCondition(), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Threshold can be between 1 to 15, but passed 16.");
        return true;
    });

    gArgs.ForceSetArg("-signblockthreshold", "10");


    BOOST_CHECK_NO_THROW(SetSignedBlocksCondition());
}

BOOST_AUTO_TEST_CASE(create_cchainparams_instance)
{
    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "10");

    SelectParams(CBaseChainParams::MAIN);
    SetSignedBlocksCondition();

    BOOST_CHECK_EQUAL(Params().GetSignedBlocksCondition().pubkeys.size(), 15);
    BOOST_CHECK_EQUAL(Params().GetSignedBlocksCondition().threshold, 10);
}

BOOST_AUTO_TEST_CASE(create_cchainparams_empty)
{
    // When pubkey is not given.
    BOOST_CHECK_EXCEPTION(MultisigCondition("", 10), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Invalid or empty publicKeyString");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(create_cchainparams_toomany)
{
    // When too much pubkeys are given.
    BOOST_CHECK_EXCEPTION(MultisigCondition(combinedPubkeyString(16), 10), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Public Keys for Signed Block are up to 15, but passed 16.");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(create_cchainparams_lowthreshold)
{
    // When too much pubkeys are given.
    BOOST_CHECK_EXCEPTION(MultisigCondition(combinedPubkeyString(15), 0), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Threshold can be between 1 to 15, but passed 0.");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(create_cchainparams_highthreshold)
{
    // When too much pubkeys are given.
    BOOST_CHECK_EXCEPTION(MultisigCondition(combinedPubkeyString(15), 16), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "Threshold can be between 1 to 15, but passed 16.");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(create_genesis_block)
{
    // This is for using CPubKey.verify().
    ECCVerifyHandle globalVerifyHandle;

    gArgs.ForceSetArg("-signblockpubkeys", combinedPubkeyString(15));
    gArgs.ForceSetArg("-signblockthreshold", "10");

    SelectParams(CBaseChainParams::MAIN);
    SetSignedBlocksCondition();

    const auto blockTime = time(0);
    const auto genesis = createGenesisBlock(Params().GetSignedBlocksCondition(), getValidPrivateKeys(15), blockTime);

    CValidationState state;
    BOOST_CHECK(CheckBlock(genesis, state, Params().GetConsensus(), true));
}


BOOST_AUTO_TEST_SUITE_END()
