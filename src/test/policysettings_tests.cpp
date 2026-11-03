// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <key.h>
#include <policy/packages.h>
#include <policy/policy.h>
#include <policy/policysettings.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <tapyrusmodes.h>
#include <test/test_tapyrus.h>
#include <util.h>

#include <stdexcept>
#include <string>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(policysettings_tests, BasicTestingSetup)

/** Builds settings from the given options alone and returns its error, or "" if it starts. */
static std::string StartupError(const std::vector<std::pair<std::string, std::string>>& options)
{
    ArgsManager args;
    for (const auto& option : options) {
        args.ForceSetArg(option.first, option.second);
    }
    try {
        PolicySettings settings(args, TAPYRUS_OP_MODE::PROD);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

/** A one-input, one-output transaction paying 1 TPC to the given script. */
static CMutableTransaction SpendTo(const CScript& scriptPubKey)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hashMalFix = InsecureRand256();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(72, 0) << std::vector<unsigned char>(33, 2);
    tx.vout.resize(1);
    tx.vout[0].nValue = ConsensusConstants::COIN;
    tx.vout[0].scriptPubKey = scriptPubKey;
    return tx;
}

static CScript BareMultisig(int32_t keys)
{
    std::vector<CPubKey> pubkeys;
    for (int32_t i = 0; i < keys; ++i) {
        CKey key;
        key.MakeNewKey(true);
        pubkeys.push_back(key.GetPubKey());
    }
    return GetScriptForMultisig(1, pubkeys);
}

BOOST_AUTO_TEST_CASE(new_option_defaults)
{
    const PolicySettings settings(ArgsManager(), TAPYRUS_OP_MODE::PROD);
    BOOST_CHECK_EQUAL(settings.maxStandardTxSize, 100000U);
    BOOST_CHECK_EQUAL(settings.minStandardTxSize, 82U);
    BOOST_CHECK_EQUAL(settings.maxScriptSigSize, 1650U);
    BOOST_CHECK_EQUAL(settings.maxBareMultisigKeys, 3U);
    BOOST_CHECK_EQUAL(settings.maxP2shSigOps, 15U);
    BOOST_CHECK_EQUAL(settings.maxReplacementCandidates, 100U);
    BOOST_CHECK_EQUAL(settings.orphanExpiry, 20 * 60);
    BOOST_CHECK_EQUAL(settings.maxPackageCount, 25U);
    BOOST_CHECK_EQUAL(settings.expectedBlockTime, 15);
}

BOOST_AUTO_TEST_CASE(new_option_ranges)
{
    BOOST_CHECK_EQUAL(StartupError({}), "");
    BOOST_CHECK_EQUAL(StartupError({{"-maxbaremultisigkeys", "0"}}), "-maxbaremultisigkeys value 0 is out of range [1, 20]");
    BOOST_CHECK_EQUAL(StartupError({{"-maxbaremultisigkeys", "21"}}), "-maxbaremultisigkeys value 21 is out of range [1, 20]");
    BOOST_CHECK_EQUAL(StartupError({{"-maxstandardtxsize", "0"}}), "-maxstandardtxsize value 0 is out of range [1, 1000000]");
    BOOST_CHECK_EQUAL(StartupError({{"-maxstandardtxsize", "1000000"}}), "");
    BOOST_CHECK_EQUAL(StartupError({{"-maxstandardtxsize", "1000001"}}), "-maxstandardtxsize value 1000001 is out of range [1, 1000000]");
    BOOST_CHECK_EQUAL(StartupError({{"-maxstandardtxsize", "1000"}, {"-minstandardtxsize", "1001"}}), "-minstandardtxsize value 1001 is out of range [0, 1000]");
    BOOST_CHECK_EQUAL(StartupError({{"-maxpackagecount", "0"}}), "-maxpackagecount value 0 is out of range [1, 4294967295]");
    BOOST_CHECK_EQUAL(StartupError({{"-orphanexpiry", "0"}}), "-orphanexpiry value 0 is out of range [1, 4294967295]");
    BOOST_CHECK_EQUAL(StartupError({{"-expectedblocktime", "0"}}), "-expectedblocktime value 0 is out of range [1, 4294967295]");
    BOOST_CHECK_EQUAL(StartupError({{"-maxp2shsigops", "-1"}}), "-maxp2shsigops value -1 is out of range [0, 4294967295]");
    // The package count may exceed the chain limits; CheckPackage bounds chains inside a package.
    BOOST_CHECK_EQUAL(StartupError({{"-maxpackagecount", "30"}, {"-limitancestorcount", "10"}}), "");
}

BOOST_AUTO_TEST_CASE(bare_multisig_options)
{
    std::string reason;
    BOOST_CHECK(IsStandardTx(CTransaction(SpendTo(BareMultisig(3))), reason));
    BOOST_CHECK(!IsStandardTx(CTransaction(SpendTo(BareMultisig(4))), reason));
    BOOST_CHECK_EQUAL(reason, "scriptpubkey");
    {
        PolicySettingsOverride fourKeys({{"-maxbaremultisigkeys", "4"}});
        BOOST_CHECK(IsStandardTx(CTransaction(SpendTo(BareMultisig(4))), reason));
    }
}

BOOST_AUTO_TEST_CASE(size_options)
{
    CKey key;
    key.MakeNewKey(true);
    const CMutableTransaction tx = SpendTo(GetScriptForDestination(key.GetPubKey().GetID()));
    const uint32_t size = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
    std::string reason;
    BOOST_CHECK(IsStandardTx(CTransaction(tx), reason));
    {
        PolicySettingsOverride smallTx({{"-maxstandardtxsize", std::to_string(size - 1)}});
        BOOST_CHECK(!IsStandardTx(CTransaction(tx), reason));
        BOOST_CHECK_EQUAL(reason, "tx-size");
    }
    {
        PolicySettingsOverride smallScriptSig({{"-maxscriptsigsize", std::to_string(tx.vin[0].scriptSig.size() - 1)}});
        BOOST_CHECK(!IsStandardTx(CTransaction(tx), reason));
        BOOST_CHECK_EQUAL(reason, "scriptsig-size");
    }
}

BOOST_AUTO_TEST_CASE(package_count_option)
{
    Package package;
    for (int32_t i = 0; i < 3; ++i) {
        CKey key;
        key.MakeNewKey(true);
        package.push_back(MakeTransactionRef(SpendTo(GetScriptForDestination(key.GetPubKey().GetID()))));
    }
    CValidationState state;
    BOOST_CHECK(CheckPackage(package, state));
    PolicySettingsOverride twoTransactions({{"-maxpackagecount", "2"}});
    BOOST_CHECK(!CheckPackage(package, state));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "package-too-many-transactions");
}

BOOST_AUTO_TEST_SUITE_END()
