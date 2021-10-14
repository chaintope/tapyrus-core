// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <key.h>
#include <validation.h>
#include <miner.h>
#include <pubkey.h>
#include <txmempool.h>
#include <random.h>
#include <script/standard.h>
#include <script/sign.h>
#include <test/test_tapyrus.h>
#include <utiltime.h>
#include <core_io.h>
#include <keystore.h>
#include <policy/policy.h>

#include <boost/test/unit_test.hpp>

bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData& txdata, TxColoredCoinBalancesMap& inColoredCoinBalances, std::vector<CScriptCheck> *pvChecks);

BOOST_AUTO_TEST_SUITE(tx_validationcache_tests)

static bool
ToMemPool(const CMutableTransaction& tx)
{
    LOCK(cs_main);

    CValidationState state;
    return AcceptToMemoryPool(mempool, state, MakeTransactionRef(tx), nullptr /* pfMissingInputs */,
                              nullptr /* plTxnReplaced */, true /* bypass_limits */, 0 /* nAbsurdFee */);
}

BOOST_FIXTURE_TEST_CASE(tx_mempool_block_doublespend, TestChainSetup)
{
    // Make sure skipping validation of transactions that were
    // validated going into the memory pool does not allow
    // double-spends in blocks to pass validation when they should not.

    for(int count : {0, 1}) //loop for Schnorrr and ECDSA signature
    {
        CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

        // Create a double-spend of mature coinbase txn:
        std::vector<CMutableTransaction> spends;
        spends.resize(2);
        for (int i = 0; i < 2; i++)
        {
            spends[i].nFeatures = 1;
            spends[i].vin.resize(1);
            spends[i].vin[0].prevout.hashMalFix = m_coinbase_txns[count]->GetHashMalFix();
            spends[i].vin[0].prevout.n = 0;
            spends[i].vout.resize(1);
            spends[i].vout[0].nValue = 11*CENT;
            spends[i].vout[0].scriptPubKey = scriptPubKey;

            // Sign:
            std::vector<unsigned char> vchSig;
            uint256 hash = SignatureHash(scriptPubKey, spends[i], 0, SIGHASH_ALL, 0, SigVersion::BASE);
            if(count == 0)
            {
                BOOST_CHECK(coinbaseKey.Sign_ECDSA(hash, vchSig));
                BOOST_CHECK(vchSig.size() <= 72 && vchSig.size() > 65);
            }
            else
            {
                BOOST_CHECK(coinbaseKey.Sign_Schnorr(hash, vchSig));
                BOOST_CHECK(vchSig.size() == 64);
            }
            vchSig.push_back((unsigned char)SIGHASH_ALL);
            spends[i].vin[0].scriptSig << vchSig;
        }

        CBlock block;

        // Test 1: block with both of those transactions should be rejected.
        block = CreateAndProcessBlock(spends, scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
        BOOST_CHECK_EQUAL(block.proof.size(), CPubKey::SCHNORR_SIGNATURE_SIZE);

        // Test 2: ... and should be rejected if spend1 is in the memory pool
        BOOST_CHECK(ToMemPool(spends[0]));
        block = CreateAndProcessBlock(spends, scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
        mempool.clear();
        BOOST_CHECK_EQUAL(block.proof.size(), CPubKey::SCHNORR_SIGNATURE_SIZE);

        // Test 3: ... and should be rejected if spend2 is in the memory pool
        BOOST_CHECK(ToMemPool(spends[1]));
        block = CreateAndProcessBlock(spends, scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() != block.GetHash());
        mempool.clear();
        BOOST_CHECK_EQUAL(block.proof.size(), CPubKey::SCHNORR_SIGNATURE_SIZE);

        // Final sanity test: first spend in mempool, second in block, that's OK:
        std::vector<CMutableTransaction> oneSpend;
        oneSpend.push_back(spends[0]);
        BOOST_CHECK(ToMemPool(spends[1]));
        block = CreateAndProcessBlock(oneSpend, scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
        BOOST_CHECK_EQUAL(block.proof.size(), CPubKey::SCHNORR_SIGNATURE_SIZE);
        // spends[1] should have been removed from the mempool when the
        // block with spends[0] is accepted:
        BOOST_CHECK_EQUAL(mempool.size(), 0U);
    }
}

// Run CheckInputs (using pcoinsTip) on the given transaction, for all script
// flags.  Test that CheckInputs passes for all flags that don't overlap with
// the failing_flags argument, but otherwise fails.
// CHECKLOCKTIMEVERIFY and CHECKSEQUENCEVERIFY (and future NOP codes that may
// get reassigned) have an interaction with DISCOURAGE_UPGRADABLE_NOPS: if
// the script flags used contain DISCOURAGE_UPGRADABLE_NOPS but don't contain
// CHECKLOCKTIMEVERIFY (or CHECKSEQUENCEVERIFY), but the script does contain
// OP_CHECKLOCKTIMEVERIFY (or OP_CHECKSEQUENCEVERIFY), then script execution
// should fail.
// Capture this interaction with the upgraded_nop argument: set it when evaluating
// any script flag that is implemented as an upgraded NOP code.
static void ValidateCheckInputsForAllFlags(const CTransaction &tx, uint32_t failing_flags, bool add_to_cache)
{
    PrecomputedTransactionData txdata(tx);
    TxColoredCoinBalancesMap inColoredCoinBalances;
    constexpr unsigned int test_flags_list[] = {SCRIPT_VERIFY_NONE,
        SCRIPT_VERIFY_SIGPUSHONLY,
        SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS,
        SCRIPT_VERIFY_CLEANSTACK,
        SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM,
        SCRIPT_VERIFY_MINIMALIF,
        SCRIPT_VERIFY_NULLFAIL,
        SCRIPT_VERIFY_WITNESS_PUBKEYTYPE,
        SCRIPT_VERIFY_CONST_SCRIPTCODE};
    // If we add many more flags, this loop can get too expensive, but we can
    // rewrite in the future to randomly pick a set of flags to evaluate.
    for (auto test_flags: test_flags_list) {
        CValidationState state;

        BOOST_CHECK(CheckInputs(tx, state, pcoinsTip.get(), true, test_flags, true, add_to_cache, txdata, inColoredCoinBalances, nullptr));

        // Test the caching
        if (add_to_cache) {
            // Check that we get a cache hit if the tx was valid
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(CheckInputs(tx, state, pcoinsTip.get(), true, test_flags, true, add_to_cache, txdata, inColoredCoinBalances, &scriptchecks));
            BOOST_CHECK(scriptchecks.empty());
        } else {
            // Check that we get script executions to check, if the transaction
            // was invalid, or we didn't add to cache.
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(CheckInputs(tx, state, pcoinsTip.get(), true, test_flags, true, add_to_cache, txdata, inColoredCoinBalances, &scriptchecks));
            BOOST_CHECK_EQUAL(scriptchecks.size(), tx.vin.size());
        }
    }
}

BOOST_FIXTURE_TEST_CASE(checkinputs_test, TestChainSetup)
{
    // Test that passing CheckInputs with one set of script flags doesn't imply
    // that we would pass again with a different set of flags.
    {
        LOCK(cs_main);
        InitScriptExecutionCache();
    }

    CScript p2pk_scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CScript p2sh_scriptPubKey = GetScriptForDestination(CScriptID(p2pk_scriptPubKey));
    CScript p2pkh_scriptPubKey = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());

    CBasicKeyStore keystore;
    keystore.AddKey(coinbaseKey);
    keystore.AddCScript(p2pk_scriptPubKey);
    TxColoredCoinBalancesMap inColoredCoinBalances;

    // flags to test: SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, SCRIPT_VERIFY_CHECKSEQUENCE_VERIFY, SCRIPT_VERIFY_NULLDUMMY, uncompressed pubkey thing

    for(int count : {0, 1}) //loop for Schnorrr and ECDSA signature
    {
        CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

        // Create 2 outputs that match the three scripts above, spending the first
        // coinbase tx.
        CMutableTransaction spend_tx;

        spend_tx.nFeatures = 1;
        spend_tx.vin.resize(1);
        spend_tx.vin[0].prevout.hashMalFix = m_coinbase_txns[count]->GetHashMalFix();
        spend_tx.vin[0].prevout.n = 0;
        spend_tx.vout.resize(3);
        spend_tx.vout[0].nValue = 11*CENT;
        spend_tx.vout[0].scriptPubKey = p2sh_scriptPubKey;
        spend_tx.vout[1].nValue = 11*CENT;
        spend_tx.vout[1].scriptPubKey = CScript() << OP_CHECKLOCKTIMEVERIFY << OP_DROP << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
        spend_tx.vout[2].nValue = 11*CENT;
        spend_tx.vout[2].scriptPubKey = CScript() << OP_CHECKSEQUENCEVERIFY << OP_DROP << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

        // Sign, with a non-DER signature
        {
            std::vector<unsigned char> vchSig;
            uint256 hash = SignatureHash(p2pk_scriptPubKey, spend_tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
            if(count == 0)
            {
                BOOST_CHECK(coinbaseKey.Sign_ECDSA(hash, vchSig));
                BOOST_CHECK(vchSig.size() <= 72 && vchSig.size() > 65);
            }
            else
            {
                BOOST_CHECK(coinbaseKey.Sign_Schnorr(hash, vchSig));
                BOOST_CHECK(vchSig.size() == 64);
            }
            vchSig.push_back((unsigned char) 0); // padding byte makes this non-DER
            vchSig.push_back((unsigned char)SIGHASH_ALL);
            spend_tx.vin[0].scriptSig << vchSig;
        }

        // Test that invalidity under a set of flags doesn't preclude validity
        // under other (eg consensus) flags.
        // spend_tx is valid according to DERSIG
        {
            LOCK(cs_main);

            CValidationState state;
            PrecomputedTransactionData ptd_spend_tx(spend_tx);

            BOOST_CHECK(!CheckInputs(spend_tx, state, pcoinsTip.get(), true, 0, true, true, ptd_spend_tx, inColoredCoinBalances, nullptr));
            BOOST_CHECK(!CheckInputs(spend_tx, state, pcoinsTip.get(), true, STANDARD_NOT_MANDATORY_VERIFY_FLAGS, true, true, ptd_spend_tx, inColoredCoinBalances, nullptr));
            BOOST_CHECK(!CheckInputs(spend_tx, state, pcoinsTip.get(), true, STANDARD_SCRIPT_VERIFY_FLAGS, true, true, ptd_spend_tx, inColoredCoinBalances, nullptr));

            // Sign, with a DER signature
            {
                std::vector<unsigned char> vchSig;
                uint256 hash = SignatureHash(p2pk_scriptPubKey, spend_tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
                if(count == 0)
                {
                    BOOST_CHECK(coinbaseKey.Sign_ECDSA(hash, vchSig));
                    BOOST_CHECK(vchSig.size() <= 72 && vchSig.size() > 65);
                }
                else
                {
                    BOOST_CHECK(coinbaseKey.Sign_Schnorr(hash, vchSig));
                    BOOST_CHECK(vchSig.size() == 64);
                }
                vchSig.push_back((unsigned char)SIGHASH_ALL);
                spend_tx.vin[0].scriptSig = CScript() << vchSig;
            }
    
            // If we call again asking for scriptchecks (as happens in
            // ConnectBlock), we should add a script check object for this -- we're
            // not caching invalidity (if that changes, delete this test case).
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(CheckInputs(spend_tx, state, pcoinsTip.get(), true, 0, true, true, ptd_spend_tx, inColoredCoinBalances, &scriptchecks));
            BOOST_CHECK_EQUAL(scriptchecks.size(), 1U);
            BOOST_CHECK(CheckInputs(spend_tx, state, pcoinsTip.get(), true, STANDARD_NOT_MANDATORY_VERIFY_FLAGS, true, true, ptd_spend_tx, inColoredCoinBalances, &scriptchecks));
            BOOST_CHECK_EQUAL(scriptchecks.size(), 2U);
            BOOST_CHECK(CheckInputs(spend_tx, state, pcoinsTip.get(), true, STANDARD_SCRIPT_VERIFY_FLAGS, true, true, ptd_spend_tx, inColoredCoinBalances, &scriptchecks));
            BOOST_CHECK_EQUAL(scriptchecks.size(), 3U);

            // Test that CheckInputs returns true iff DERSIG-enforcing flags are
            // not present.  Don't add these checks to the cache, so that we can
            // test later that block validation works fine in the absence of cached
            // successes.
            ValidateCheckInputsForAllFlags(spend_tx, STANDARD_SCRIPT_VERIFY_FLAGS, false);
        }

        // And if we produce a block with this tx, it should be valid (DERSIG not
        // enabled yet), even though there's no cache entry.
        CBlock block;

        block = CreateAndProcessBlock({spend_tx}, p2pk_scriptPubKey);
        BOOST_CHECK(chainActive.Tip()->GetBlockHash() == block.GetHash());
        BOOST_CHECK(pcoinsTip->GetBestBlock() == block.GetHash());
        BOOST_CHECK_EQUAL(block.proof.size(), CPubKey::SCHNORR_SIGNATURE_SIZE);

        LOCK(cs_main);

        // Test P2SH: construct a transaction that is valid without P2SH, and
        // then test validity with P2SH.
        {
            CMutableTransaction invalid_under_p2sh_tx;
            invalid_under_p2sh_tx.nFeatures = 1;
            invalid_under_p2sh_tx.vin.resize(1);
            invalid_under_p2sh_tx.vin[0].prevout.hashMalFix = spend_tx.GetHashMalFix();
            invalid_under_p2sh_tx.vin[0].prevout.n = 0;
            invalid_under_p2sh_tx.vout.resize(1);
            invalid_under_p2sh_tx.vout[0].nValue = 11*CENT;
            invalid_under_p2sh_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;
            std::vector<unsigned char> vchSig2(p2pk_scriptPubKey.begin(), p2pk_scriptPubKey.end());
            invalid_under_p2sh_tx.vin[0].scriptSig << vchSig2;

            CValidationState state;
            PrecomputedTransactionData ptd_spend_tx(invalid_under_p2sh_tx);
            BOOST_CHECK(!CheckInputs(invalid_under_p2sh_tx, state, pcoinsTip.get(), true, 0, true, true, ptd_spend_tx, inColoredCoinBalances, nullptr));
            BOOST_CHECK(!CheckInputs(invalid_under_p2sh_tx, state, pcoinsTip.get(), true, STANDARD_NOT_MANDATORY_VERIFY_FLAGS, true, true, ptd_spend_tx, inColoredCoinBalances, nullptr));
            BOOST_CHECK(!CheckInputs(invalid_under_p2sh_tx, state, pcoinsTip.get(), true, STANDARD_SCRIPT_VERIFY_FLAGS, true, true, ptd_spend_tx, inColoredCoinBalances, nullptr));
        }

        // Test CHECKLOCKTIMEVERIFY
        {
            CMutableTransaction invalid_with_cltv_tx;
            invalid_with_cltv_tx.nFeatures = 1;
            invalid_with_cltv_tx.nLockTime = 100;
            invalid_with_cltv_tx.vin.resize(1);
            invalid_with_cltv_tx.vin[0].prevout.hashMalFix = spend_tx.GetHashMalFix();
            invalid_with_cltv_tx.vin[0].prevout.n = 1;
            invalid_with_cltv_tx.vin[0].nSequence = 0;
            invalid_with_cltv_tx.vout.resize(1);
            invalid_with_cltv_tx.vout[0].nValue = 11*CENT;
            invalid_with_cltv_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

            // Sign
            std::vector<unsigned char> vchSig;
            uint256 hash = SignatureHash(spend_tx.vout[1].scriptPubKey, invalid_with_cltv_tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
            if(count == 0)
            {
                BOOST_CHECK(coinbaseKey.Sign_ECDSA(hash, vchSig));
                BOOST_CHECK(vchSig.size() <= 72 && vchSig.size() > 65);
            }
            else
            {
                BOOST_CHECK(coinbaseKey.Sign_Schnorr(hash, vchSig));
                BOOST_CHECK(vchSig.size() == 64);
            }
            vchSig.push_back((unsigned char)SIGHASH_ALL);
            invalid_with_cltv_tx.vin[0].scriptSig = CScript() << vchSig << 101;

            CValidationState state;
            PrecomputedTransactionData txdata(invalid_with_cltv_tx);
            BOOST_CHECK(!CheckInputs(invalid_with_cltv_tx, state, pcoinsTip.get(), true, 0, true, true, txdata, inColoredCoinBalances, nullptr));
            BOOST_CHECK(!CheckInputs(invalid_with_cltv_tx, state, pcoinsTip.get(), true, STANDARD_NOT_MANDATORY_VERIFY_FLAGS, true, true, txdata, inColoredCoinBalances, nullptr));
            BOOST_CHECK(!CheckInputs(invalid_with_cltv_tx, state, pcoinsTip.get(), true, STANDARD_SCRIPT_VERIFY_FLAGS, true, true, txdata, inColoredCoinBalances, nullptr));

            // Make it valid, and check again
            invalid_with_cltv_tx.vin[0].scriptSig = CScript() << vchSig << 100;
            BOOST_CHECK(CheckInputs(invalid_with_cltv_tx, state, pcoinsTip.get(), true, 0, true, true, txdata, inColoredCoinBalances, nullptr));
            BOOST_CHECK(CheckInputs(invalid_with_cltv_tx, state, pcoinsTip.get(), true, STANDARD_NOT_MANDATORY_VERIFY_FLAGS, true, true, txdata, inColoredCoinBalances, nullptr));
            BOOST_CHECK(CheckInputs(invalid_with_cltv_tx, state, pcoinsTip.get(), true, STANDARD_SCRIPT_VERIFY_FLAGS, true, true, txdata, inColoredCoinBalances, nullptr));
        }

        // TEST CHECKSEQUENCEVERIFY
        {
            CMutableTransaction invalid_with_csv_tx;
            invalid_with_csv_tx.nFeatures = 1;
            invalid_with_csv_tx.vin.resize(1);
            invalid_with_csv_tx.vin[0].prevout.hashMalFix = spend_tx.GetHashMalFix();
            invalid_with_csv_tx.vin[0].prevout.n = 2;
            invalid_with_csv_tx.vin[0].nSequence = 100;
            invalid_with_csv_tx.vout.resize(1);
            invalid_with_csv_tx.vout[0].nValue = 11*CENT;
            invalid_with_csv_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

            // Sign
            std::vector<unsigned char> vchSig;
            uint256 hash = SignatureHash(spend_tx.vout[2].scriptPubKey, invalid_with_csv_tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
            if(count == 0)
            {
                BOOST_CHECK(coinbaseKey.Sign_ECDSA(hash, vchSig));
                BOOST_CHECK(vchSig.size() <= 72 && vchSig.size() > 65);
            }
            else
            {
                BOOST_CHECK(coinbaseKey.Sign_Schnorr(hash, vchSig));
                BOOST_CHECK(vchSig.size() == 64);
            }
            vchSig.push_back((unsigned char)SIGHASH_ALL);
            invalid_with_csv_tx.vin[0].scriptSig = CScript() << vchSig << 101;

            CValidationState state;
            PrecomputedTransactionData txdata(invalid_with_csv_tx);

            BOOST_CHECK(!CheckInputs(invalid_with_csv_tx, state, pcoinsTip.get(), true, 0, true, true, txdata, inColoredCoinBalances, nullptr));
            BOOST_CHECK(!CheckInputs(invalid_with_csv_tx, state, pcoinsTip.get(), true, STANDARD_NOT_MANDATORY_VERIFY_FLAGS, true, true, txdata, inColoredCoinBalances, nullptr));
            BOOST_CHECK(!CheckInputs(invalid_with_csv_tx, state, pcoinsTip.get(), true, STANDARD_SCRIPT_VERIFY_FLAGS, true, true, txdata, inColoredCoinBalances, nullptr));

            // Make it valid, and check again
            invalid_with_csv_tx.vin[0].scriptSig = CScript() << vchSig << 100;
            BOOST_CHECK(CheckInputs(invalid_with_csv_tx, state, pcoinsTip.get(), true, 0, true, true, txdata, inColoredCoinBalances, nullptr));
            BOOST_CHECK(CheckInputs(invalid_with_csv_tx, state, pcoinsTip.get(), true, STANDARD_NOT_MANDATORY_VERIFY_FLAGS, true, true, txdata, inColoredCoinBalances, nullptr));
            BOOST_CHECK(CheckInputs(invalid_with_csv_tx, state, pcoinsTip.get(), true, STANDARD_SCRIPT_VERIFY_FLAGS, true, true, txdata, inColoredCoinBalances, nullptr));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
