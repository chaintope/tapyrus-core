// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Verifies, with runnable proofs, the "risky but permitted" operator
// combinations documented for the Tapyrus script interpreter:
//
//  1. Zero-of-N CHECKMULTISIG is a trivial anyone-can-spend pass.
//  2. OP_COLOR accepts a color id assembled by prior stack manipulation,
//     not just an immediate push -- closed off only at the consensus layer
//     (CheckColorIdentifierValidity), not by the interpreter itself.
//  3. OP_CHECKDATASIG(VERIFY) has no built-in binding to the spending
//     transaction, so a (signature, message) pair replays across unrelated
//     transactions -- unlike OP_CHECKSIG.
//  4. OP_CHECKSIG calls made independently (outside OP_CHECKMULTISIG) can
//     mix ECDSA and Schnorr freely; only CHECKMULTISIG enforces uniformity.
//  5. OP_CHECKLOCKTIMEVERIFY can be combined directly with OP_COLOR in a
//     single scriptPubKey at the interpreter level, even though the design
//     intends CLTV/CSV to live only inside a CP2SH redeem script.
//
// Each finding is exercised with multiple cases: the risky behavior itself,
// plus a control/contrast case showing where the behavior does NOT apply,
// so a future change that narrows (or accidentally widens) these behaviors
// will be caught here.

#include <key.h>
#include <keystore.h>
#include <coloridentifier.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/standard.h>
#include <test/test_tapyrus.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(script_risky_combinations_tests, BasicTestingSetup)

namespace {

CMutableTransaction BuildCreditingTx(const CScript& scriptPubKey, CAmount nValue = 0)
{
    CMutableTransaction txCredit;
    txCredit.nFeatures = 1;
    txCredit.nLockTime = 0;
    txCredit.vin.resize(1);
    txCredit.vout.resize(1);
    txCredit.vin[0].prevout.SetNull();
    txCredit.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    txCredit.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txCredit.vout[0].scriptPubKey = scriptPubKey;
    txCredit.vout[0].nValue = nValue;
    return txCredit;
}

CMutableTransaction BuildSpendingTx(const CScript& scriptSig, const CTransaction& txCredit,
                                    uint32_t nSequence = CTxIn::SEQUENCE_FINAL, uint32_t nLockTime = 0)
{
    CMutableTransaction txSpend;
    txSpend.nFeatures = 1;
    txSpend.nLockTime = nLockTime;
    txSpend.vin.resize(1);
    txSpend.vout.resize(1);
    txSpend.vin[0].prevout.hashMalFix = txCredit.GetHashMalFix();
    txSpend.vin[0].prevout.n = 0;
    txSpend.vin[0].scriptSig = scriptSig;
    txSpend.vin[0].nSequence = nSequence;
    txSpend.vout[0].scriptPubKey = CScript();
    txSpend.vout[0].nValue = txCredit.vout[0].nValue;
    return txSpend;
}

// Verifies exactly as production does: a fresh ColorIdentifier per call
// (see scriptcheck.h) unless the caller wants to inspect what OP_COLOR set.
bool Verify(const CScript& scriptSig, const CScript& scriptPubKey, const CMutableTransaction& spendMut,
            CAmount amount, unsigned int flags, ScriptError* err, ColorIdentifier* colorIdOut = nullptr)
{
    CTransaction spend(spendMut);
    PrecomputedTransactionData txdata(spend);
    ColorIdentifier localColorId;
    ColorIdentifier& colorId = colorIdOut ? *colorIdOut : localColorId;
    return VerifyScript(scriptSig, scriptPubKey, flags,
                         TransactionSignatureChecker(&spend, 0, amount, txdata), colorId, err);
}

} // namespace

// ---------------------------------------------------------------------------
// Risk 1: Zero-of-N CHECKMULTISIG is a trivial anyone-can-spend pass.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(risk_checkmultisig_zero_of_n)
{
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    CPubKey pub1 = key1.GetPubKey();
    CPubKey pub2 = key2.GetPubKey();

    // 1a: "0-of-2" multisig -- looks like it needs two keys, actually needs none.
    {
        CScript scriptPubKey = CScript() << OP_0 << ToByteVector(pub1) << ToByteVector(pub2) << OP_2 << OP_CHECKMULTISIG;
        CScript scriptSig = CScript() << OP_0; // only the mandatory dummy element; zero real signatures
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey);
        CMutableTransaction spend = BuildSpendingTx(scriptSig, CTransaction(credit));
        ScriptError err;
        bool ok = Verify(scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err);
        BOOST_CHECK_MESSAGE(ok, "0-of-2 CHECKMULTISIG should be spendable with zero signatures: " + std::string(ScriptErrorString(err)));
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
    }

    // 1b: NULLDUMMY is still enforced even in the trivial-pass case: a non-empty
    // dummy element still fails, proving the risk is specifically "M == 0", not
    // a general relaxation of CHECKMULTISIG's stack bookkeeping.
    {
        CScript scriptPubKey = CScript() << OP_0 << ToByteVector(pub1) << ToByteVector(pub2) << OP_2 << OP_CHECKMULTISIG;
        CScript scriptSig = CScript() << std::vector<unsigned char>{0xab, 0xcd}; // non-empty dummy (2 bytes, so this isn't itself a MINIMALDATA violation)
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey);
        CMutableTransaction spend = BuildSpendingTx(scriptSig, CTransaction(credit));
        ScriptError err;
        bool ok = Verify(scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err);
        BOOST_CHECK(!ok);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_SIG_NULLDUMMY);
    }

    // 1c: degenerate 0-of-0 also passes trivially.
    {
        CScript scriptPubKey = CScript() << OP_0 << OP_0 << OP_CHECKMULTISIG;
        CScript scriptSig = CScript() << OP_0;
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey);
        CMutableTransaction spend = BuildSpendingTx(scriptSig, CTransaction(credit));
        ScriptError err;
        bool ok = Verify(scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err);
        BOOST_CHECK(ok);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
    }

    // 1d: control -- 1-of-2 with no real signature correctly fails. The risk
    // is specific to M == 0, not a general multisig weakness.
    {
        CScript scriptPubKey = CScript() << OP_1 << ToByteVector(pub1) << ToByteVector(pub2) << OP_2 << OP_CHECKMULTISIG;
        CScript scriptSig = CScript() << OP_0; // dummy only; M=1 signature is required but absent
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey);
        CMutableTransaction spend = BuildSpendingTx(scriptSig, CTransaction(credit));
        ScriptError err;
        bool ok = Verify(scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err);
        BOOST_CHECK(!ok);
        // CHECKMULTISIG's mandatory extra "dummy" pop means it needs two
        // stack items here (dummy + one real signature); scriptSig supplies
        // only one, so this fails on stack underflow before signature
        // verification is ever reached.
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_INVALID_STACK_OPERATION);
    }
}

// ---------------------------------------------------------------------------
// Risk 2: OP_COLOR accepts a color id assembled by prior stack manipulation.
// Closed off only by CheckColorIdentifierValidity (consensus layer), not by
// the interpreter -- so we show the interpreter alone says OK, while the
// standardness/consensus template matcher (IsColoredPayToPubkeyHash) would
// have refused to recognize the script as colored at all.
//
// Using STANDARD_SCRIPT_VERIFY_FLAGS (which includes SCRIPT_VERIFY_CP2SH_COLORED)
// is not significant here: that flag only gates redeem-script evaluation for
// CP2SH-shaped scriptPubKeys (IsColoredPayToScriptHash()), and the SWAP/ALTSTACK
// scripts below aren't P2SH-shaped at all -- they're plain scripts evaluated
// directly by EvalScript, so the flag never engages.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(risk_opcolor_stack_manipulation)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    COutPoint outA(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
    COutPoint outB(uint256S("0000000000000000000000000000000000000000000000000000000000000002"), 0);
    ColorIdentifier realCid(outA, TokenTypes::REISSUABLE);
    ColorIdentifier decoyCid(outB, TokenTypes::NON_REISSUABLE);
    BOOST_REQUIRE(realCid != decoyCid);

    // 2a: SWAP -- the color id actually consumed is chosen by the scriptSig,
    // not fixed by the scriptPubKey text.
    {
        CScript scriptPubKey = CScript() << decoyCid.toVector() << OP_SWAP << OP_COLOR << OP_DROP
                                          << OP_DUP << OP_HASH160 << ToByteVector(pub.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG;
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey);
        CMutableTransaction spend = BuildSpendingTx(CScript(), CTransaction(credit));
        uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, credit.vout[0].nValue);
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key.Sign_ECDSA(hash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        spend.vin[0].scriptSig = CScript() << sig << ToByteVector(pub) << realCid.toVector();

        ColorIdentifier resultColorId;
        ScriptError err;
        bool ok = Verify(spend.vin[0].scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err, &resultColorId);
        BOOST_CHECK_MESSAGE(ok, "SWAP-derived OP_COLOR should be accepted by the raw interpreter: " + std::string(ScriptErrorString(err)));
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
        BOOST_CHECK_MESSAGE(resultColorId == realCid, "OP_COLOR should bind to the SWAP-selected id, not the literal one written in scriptPubKey");
        BOOST_CHECK(resultColorId != decoyCid);

        // The consensus-layer template matcher does not recognize this shape at all.
        std::vector<unsigned char> hashOut, cidOut;
        BOOST_CHECK(!scriptPubKey.IsColoredPayToPubkeyHash(hashOut, cidOut));
        ColorIdentifier fromTemplate = GetColorIdFromScript(scriptPubKey);
        BOOST_CHECK(fromTemplate.type == TokenTypes::NONE);
    }

    // 2b: ALTSTACK -- a different technique (stash-and-restore) achieves the
    // same result, showing this isn't a one-off quirk of SWAP specifically.
    {
        CScript scriptPubKey = CScript() << decoyCid.toVector() << OP_TOALTSTACK << OP_COLOR << OP_FROMALTSTACK << OP_DROP
                                          << OP_DUP << OP_HASH160 << ToByteVector(pub.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG;
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey);
        CMutableTransaction spend = BuildSpendingTx(CScript(), CTransaction(credit));
        uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, credit.vout[0].nValue);
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key.Sign_ECDSA(hash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        spend.vin[0].scriptSig = CScript() << sig << ToByteVector(pub) << realCid.toVector();

        ColorIdentifier resultColorId;
        ScriptError err;
        bool ok = Verify(spend.vin[0].scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err, &resultColorId);
        BOOST_CHECK_MESSAGE(ok, "ALTSTACK-derived OP_COLOR should be accepted by the raw interpreter: " + std::string(ScriptErrorString(err)));
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
        BOOST_CHECK(resultColorId == realCid);

        std::vector<unsigned char> hashOut, cidOut;
        BOOST_CHECK(!scriptPubKey.IsColoredPayToPubkeyHash(hashOut, cidOut));
    }

    // 2c: control -- the canonical CP2PKH template (immediate push then
    // OP_COLOR) IS recognized by the consensus-layer matcher, confirming the
    // mismatch above is about non-canonical shapes specifically.
    {
        ColorIdentifier cid(outA, TokenTypes::REISSUABLE);
        CScript canonical = CScript() << cid.toVector() << OP_COLOR << OP_DUP << OP_HASH160
                                       << ToByteVector(pub.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        std::vector<unsigned char> hashOut, cidOut;
        BOOST_CHECK(canonical.IsColoredPayToPubkeyHash(hashOut, cidOut));
    }
}

// ---------------------------------------------------------------------------
// Risk 3: OP_CHECKDATASIG(VERIFY) has no built-in binding to the spending
// transaction -- a (signature, message) pair replays across unrelated
// transactions. Contrasted with OP_CHECKSIG, which is bound to the tx via
// SignatureHash and does NOT replay.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(risk_checkdatasig_replay)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    // 3a: sign a message once; the same scriptSig satisfies two wildly
    // different spending transactions (different amount, sequence, locktime,
    // and even a different output).
    {
        std::vector<unsigned char> message = {'h', 'e', 'l', 'l', 'o'};
        uint256 msgHash;
        CSHA256().Write(message.data(), message.size()).Finalize(msgHash.begin());
        std::vector<unsigned char> vchSig;
        BOOST_REQUIRE(key.Sign_ECDSA(msgHash, vchSig)); // no hashtype byte for data signatures

        CScript scriptPubKey = CScript() << ToByteVector(pub) << OP_CHECKDATASIGVERIFY << OP_1;
        CScript scriptSig = CScript() << vchSig << message;

        CMutableTransaction creditA = BuildCreditingTx(scriptPubKey, 1000);
        CMutableTransaction spendA = BuildSpendingTx(scriptSig, CTransaction(creditA), CTxIn::SEQUENCE_FINAL, 0);
        ScriptError errA;
        bool okA = Verify(scriptSig, scriptPubKey, spendA, creditA.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &errA);
        BOOST_CHECK(okA);
        BOOST_CHECK_EQUAL(errA, SCRIPT_ERR_OK);

        CMutableTransaction creditB = BuildCreditingTx(scriptPubKey, 999999);
        CMutableTransaction spendB = BuildSpendingTx(scriptSig, CTransaction(creditB), 12345, 700000);
        spendB.vout[0].scriptPubKey = CScript() << OP_RETURN; // unrelated output too
        ScriptError errB;
        bool okB = Verify(scriptSig, scriptPubKey, spendB, creditB.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &errB);
        BOOST_CHECK_MESSAGE(okB, "the identical (sig, message) pair should replay across an unrelated transaction context");
        BOOST_CHECK_EQUAL(errB, SCRIPT_ERR_OK);
    }

    // 3b: control -- OP_CHECKSIG does NOT replay: the same scriptSig fails
    // against a transaction with a different nLockTime, because the
    // signature is over that transaction's SignatureHash.
    {
        CScript scriptPubKey = CScript() << ToByteVector(pub) << OP_CHECKSIG;
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey, 1000);
        CMutableTransaction spendOriginal = BuildSpendingTx(CScript(), CTransaction(credit), CTxIn::SEQUENCE_FINAL, 0);
        uint256 sighash = SignatureHash(scriptPubKey, spendOriginal, 0, SIGHASH_ALL, credit.vout[0].nValue);
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key.Sign_ECDSA(sighash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        CScript scriptSig = CScript() << sig;

        CMutableTransaction spendSame = BuildSpendingTx(scriptSig, CTransaction(credit), CTxIn::SEQUENCE_FINAL, 0);
        ScriptError errSame;
        bool okSame = Verify(scriptSig, scriptPubKey, spendSame, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &errSame);
        BOOST_CHECK(okSame);

        CMutableTransaction spendDifferent = BuildSpendingTx(scriptSig, CTransaction(credit), CTxIn::SEQUENCE_FINAL, 999);
        ScriptError errDifferent;
        bool okDifferent = Verify(scriptSig, scriptPubKey, spendDifferent, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &errDifferent);
        BOOST_CHECK_MESSAGE(!okDifferent, "OP_CHECKSIG must NOT replay across a transaction with a different nLockTime");
    }
}

// ---------------------------------------------------------------------------
// Risk 4: ECDSA and Schnorr can be freely mixed outside OP_CHECKMULTISIG.
// Contrasted with OP_CHECKMULTISIG, which enforces a single scheme.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(risk_mixed_signature_schemes)
{
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    CPubKey pub1 = key1.GetPubKey();
    CPubKey pub2 = key2.GetPubKey();

    CScript scriptPubKey = CScript() << OP_IF << ToByteVector(pub1) << OP_CHECKSIG
                                      << OP_ELSE << ToByteVector(pub2) << OP_CHECKSIG << OP_ENDIF;

    // 4a: branch A satisfied with an ECDSA signature.
    {
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey);
        CMutableTransaction spend = BuildSpendingTx(CScript(), CTransaction(credit));
        uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, credit.vout[0].nValue);
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key1.Sign_ECDSA(hash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        spend.vin[0].scriptSig = CScript() << sig << OP_1;
        ScriptError err;
        bool ok = Verify(spend.vin[0].scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err);
        BOOST_CHECK(ok);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
    }

    // 4b: the SAME scriptPubKey's other branch satisfied with a Schnorr
    // signature -- no uniformity is required across independent CHECKSIGs.
    {
        CMutableTransaction credit = BuildCreditingTx(scriptPubKey);
        CMutableTransaction spend = BuildSpendingTx(CScript(), CTransaction(credit));
        uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, credit.vout[0].nValue);
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key2.Sign_Schnorr(hash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        spend.vin[0].scriptSig = CScript() << sig << OP_0;
        ScriptError err;
        bool ok = Verify(spend.vin[0].scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err);
        BOOST_CHECK(ok);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
    }

    // 4c: control -- OP_CHECKMULTISIG rejects the same mix (one ECDSA + one
    // Schnorr signature) with a dedicated error, proving the risk really is
    // specific to standalone CHECKSIG calls.
    {
        CScript msPubKey = CScript() << OP_2 << ToByteVector(pub1) << ToByteVector(pub2) << OP_2 << OP_CHECKMULTISIG;
        CMutableTransaction credit = BuildCreditingTx(msPubKey);
        CMutableTransaction spend = BuildSpendingTx(CScript(), CTransaction(credit));
        uint256 hash = SignatureHash(msPubKey, spend, 0, SIGHASH_ALL, credit.vout[0].nValue);
        std::vector<unsigned char> sig1, sig2;
        BOOST_REQUIRE(key1.Sign_ECDSA(hash, sig1));
        sig1.push_back((unsigned char)SIGHASH_ALL);
        BOOST_REQUIRE(key2.Sign_Schnorr(hash, sig2));
        sig2.push_back((unsigned char)SIGHASH_ALL);
        spend.vin[0].scriptSig = CScript() << OP_0 << sig1 << sig2;
        ScriptError err;
        bool ok = Verify(spend.vin[0].scriptSig, msPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err);
        BOOST_CHECK(!ok);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_MIXED_SCHEME_MULTISIG);
    }
}

// ---------------------------------------------------------------------------
// Risk 5: OP_CHECKLOCKTIMEVERIFY can be combined directly with OP_COLOR in a
// single scriptPubKey at the interpreter level, even though the design
// intends time-locked colored coins to live only inside a CP2SH redeem
// script (CheckColorIdentifierValidity enforces that at the consensus
// layer; the interpreter itself has no opinion).
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(risk_opcolor_with_timelock)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    COutPoint outpoint(uint256S("0000000000000000000000000000000000000000000000000000000000000003"), 0);
    ColorIdentifier colorId(outpoint, TokenTypes::REISSUABLE);
    const int64_t lockHeight = 500000; // block-height style (< LOCKTIME_THRESHOLD)

    CScript scriptPubKey = CScript() << colorId.toVector() << OP_COLOR
                                      << CScriptNum(lockHeight) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                      << OP_DUP << OP_HASH160 << ToByteVector(pub.GetID())
                                      << OP_EQUALVERIFY << OP_CHECKSIG;

    CMutableTransaction credit = BuildCreditingTx(scriptPubKey);

    // 5a: locktime satisfied -> interpreter accepts the direct OP_COLOR + CLTV combination.
    {
        CMutableTransaction spend = BuildSpendingTx(CScript(), CTransaction(credit), 0, lockHeight);
        uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, credit.vout[0].nValue);
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key.Sign_ECDSA(hash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        spend.vin[0].scriptSig = CScript() << sig << ToByteVector(pub);

        ColorIdentifier resultColorId;
        ScriptError err;
        bool ok = Verify(spend.vin[0].scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err, &resultColorId);
        BOOST_CHECK_MESSAGE(ok, "OP_COLOR directly combined with CHECKLOCKTIMEVERIFY should be accepted by the raw interpreter: " + std::string(ScriptErrorString(err)));
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
        BOOST_CHECK(resultColorId == colorId);

        // The consensus-layer template matcher does not recognize this shape
        // as colored at all -- CheckColorIdentifierValidity, not the
        // interpreter, is what actually keeps this pattern out of real blocks.
        std::vector<unsigned char> hashOut, cidOut;
        BOOST_CHECK(!scriptPubKey.IsColoredPayToPubkeyHash(hashOut, cidOut));
        ColorIdentifier fromTemplate = GetColorIdFromScript(scriptPubKey);
        BOOST_CHECK(fromTemplate.type == TokenTypes::NONE);
    }

    // 5b: control -- locktime not yet satisfied correctly fails, proving the
    // CLTV portion of the combined script is genuinely being enforced (this
    // isn't just OP_COLOR silently swallowing the whole script).
    {
        CMutableTransaction spend = BuildSpendingTx(CScript(), CTransaction(credit), 0, lockHeight - 1);
        uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, credit.vout[0].nValue);
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(key.Sign_ECDSA(hash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        spend.vin[0].scriptSig = CScript() << sig << ToByteVector(pub);

        ScriptError err;
        bool ok = Verify(spend.vin[0].scriptSig, scriptPubKey, spend, credit.vout[0].nValue, STANDARD_SCRIPT_VERIFY_FLAGS, &err);
        BOOST_CHECK(!ok);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
    }
}

BOOST_AUTO_TEST_SUITE_END()
