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

extern void noui_connect();

struct ChainParamsTestingSetup {

    explicit ChainParamsTestingSetup(const std::string& chainName= TAPYRUS_MODES::MAIN)
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
    }

    ~ChainParamsTestingSetup()
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

BOOST_FIXTURE_TEST_SUITE(chainparams_tests, ChainParamsTestingSetup)

BOOST_AUTO_TEST_CASE(default_params_main)
{
    //main net
    BOOST_CHECK_NO_THROW(SelectParams(TAPYRUS_OP_MODE::MAIN));
    
    BOOST_CHECK(Params().GetRPCPort() == 2377);
    BOOST_CHECK(Params().GetDefaultPort() == 2357);
}

BOOST_AUTO_TEST_CASE(default_params_regtest)
{
    //regtest
    gArgs.ForceSetArg("-regtest", "1");
    BOOST_CHECK_NO_THROW(SelectParams(TAPYRUS_OP_MODE::REGTEST));

    BOOST_CHECK(Params().GetRPCPort() == 12381);
    BOOST_CHECK(Params().GetDefaultPort() == 12383);
}

BOOST_AUTO_TEST_CASE(unknown_mode_test)
{
    BOOST_CHECK_EXCEPTION(SelectParams((TAPYRUS_OP_MODE)5), std::runtime_error, [] (const std::runtime_error& ex) {
        BOOST_CHECK_EQUAL(ex.what(), "CreateChainParams: Unknown mode.");
        return true;
    });
}

BOOST_AUTO_TEST_CASE(custom_networkid_main)
{
    //main net
    gArgs.ForceSetArg("-networkid", "2");
    BOOST_CHECK_NO_THROW(SelectParams(TAPYRUS_OP_MODE::MAIN));
    
    BOOST_CHECK(Params().GetRPCPort() == 2377);
    BOOST_CHECK(Params().GetDefaultPort() == 2357);
}

BOOST_AUTO_TEST_CASE(custom_networkid_regtest)
{
    //regtest
    gArgs.ForceSetArg("-regtest", "1");
    gArgs.ForceSetArg("-networkid", "1939510133");
    BOOST_CHECK_NO_THROW(SelectParams(TAPYRUS_OP_MODE::REGTEST));

    BOOST_CHECK(Params().GetRPCPort() == 12381);
    BOOST_CHECK(Params().GetDefaultPort() == 12383);
}

BOOST_AUTO_TEST_CASE(default_base_params_tests)
{
    //main net
    gArgs.ForceSetArg("-networkid", "1");
    writeTestGenesisBlockToFile(GetDataDir(), "genesis.1");
    BOOST_CHECK_NO_THROW(SelectParams(TAPYRUS_OP_MODE::MAIN));
    BOOST_CHECK_NO_THROW(SelectBaseParams(TAPYRUS_OP_MODE::MAIN));
    BOOST_CHECK(BaseParams().NetworkIDString() == "1");
    BOOST_CHECK(BaseParams().getDataDir() == "main-1");

    CMessageHeader::MessageStartChars pchMessageStart = {0x01, 0xFF, 0xF0, 0x00};
    BOOST_CHECK(memcmp( BaseParams().MessageStart(), pchMessageStart, sizeof(pchMessageStart)) == 0);

    //regtest
    gArgs.ForceSetArg("-regtest", "1");
    gArgs.ForceSetArg("-networkid", "1905960821");
    writeTestGenesisBlockToFile(GetDataDir(), "genesis.1905960821");
    BOOST_CHECK_NO_THROW(SelectParams(TAPYRUS_OP_MODE::REGTEST));
    BOOST_CHECK_NO_THROW(SelectBaseParams(TAPYRUS_OP_MODE::REGTEST));
    BOOST_CHECK(BaseParams().NetworkIDString() == "1905960821");
    BOOST_CHECK(BaseParams().getDataDir() == "regtest-1905960821");

    CMessageHeader::MessageStartChars pchMessageStart1 = {0x73, 0x9A, 0x97, 0x74};
    BOOST_CHECK(memcmp(BaseParams().MessageStart(), pchMessageStart1, sizeof(pchMessageStart1)) == 0);
}

BOOST_AUTO_TEST_CASE(custom_networkId_test)
{
    gArgs.ForceSetArg("-networkid", "2");
    writeTestGenesisBlockToFile(GetDataDir(), "genesis.2");

    BOOST_CHECK_NO_THROW(SelectParams(TAPYRUS_OP_MODE::MAIN));
    BOOST_CHECK_NO_THROW(SelectBaseParams(TAPYRUS_OP_MODE::MAIN));
    BOOST_CHECK(BaseParams().NetworkIDString() == "2");
    BOOST_CHECK(BaseParams().getDataDir() == "main-2");
    
    CMessageHeader::MessageStartChars pchMessageStart = {0x01, 0xFF, 0xF0, 0x01};
    BOOST_CHECK(memcmp(BaseParams().MessageStart(), pchMessageStart, sizeof(pchMessageStart)) == 0);

    gArgs.ForceSetArg("-regtest", "1");
    gArgs.ForceSetArg("-networkid", "1939510133");
    writeTestGenesisBlockToFile(GetDataDir(), "genesis.1939510133");

    BOOST_CHECK_NO_THROW(SelectParams(TAPYRUS_OP_MODE::REGTEST));
    BOOST_CHECK_NO_THROW(SelectBaseParams(TAPYRUS_OP_MODE::REGTEST));
    BOOST_CHECK(BaseParams().NetworkIDString() == "1939510133");
    BOOST_CHECK(BaseParams().getDataDir() == "regtest-1939510133");
    
    CMessageHeader::MessageStartChars pchMessageStart1 = {0x75, 0x9A, 0x83, 0x74};
    BOOST_CHECK(memcmp(BaseParams().MessageStart(), pchMessageStart1, sizeof(pchMessageStart1)) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
