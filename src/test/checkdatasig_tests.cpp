// Copyright (c) 2018 The Bitcoin developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_tapyrus.h>

#include <policy/policy.h>
#include <script/interpreter.h>

#include <boost/test/unit_test.hpp>

#include <array>

typedef std::vector<uint8_t> valtype;
typedef std::vector<valtype> stacktype;

BOOST_FIXTURE_TEST_SUITE(checkdatasig_tests, BasicTestingSetup)

const uint8_t vchPrivkey[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

struct KeyData {
    CKey privkey, privkeyC;
    CPubKey pubkey, pubkeyC, pubkeyH;

    KeyData() {
        privkey.Set(vchPrivkey, vchPrivkey + 32, false);
        privkeyC.Set(vchPrivkey, vchPrivkey + 32, true);
        pubkey = privkey.GetPubKey();
        pubkeyH = privkey.GetPubKey();
        pubkeyC = privkeyC.GetPubKey();
        *const_cast<uint8_t *>(&pubkeyH[0]) = 0x06 | (pubkeyH[64] & 1);
    }
};

static void CheckError(uint32_t flags, const stacktype &original_stack,
                       const CScript &script, ScriptError expected) {
    
    ScriptError err = SCRIPT_ERR_OK;
    CMutableTransaction txCredit;
    txCredit.nFeatures = 1;
    txCredit.nLockTime = 0;
    txCredit.vin.resize(1);
    txCredit.vout.resize(1);
    txCredit.vin[0].prevout.SetNull();
    txCredit.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    txCredit.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txCredit.vout[0].scriptPubKey = script;
    txCredit.vout[0].nValue = 0;

    MutableTransactionSignatureChecker checker(&txCredit, 0, 0);

    stacktype stack{original_stack};
    ColorIdentifier colorId;
    bool r = EvalScript(stack, script, flags, checker, SigVersion::BASE, &colorId, &err);
    BOOST_CHECK(!r);
    BOOST_CHECK_EQUAL(err, expected);
}

static void CheckPass(uint32_t flags, const stacktype &original_stack,
                      const CScript &script, const stacktype &expected) {

    ScriptError err = SCRIPT_ERR_OK;
    CMutableTransaction txCredit;
    txCredit.nFeatures = 1;
    txCredit.nLockTime = 0;
    txCredit.vin.resize(1);
    txCredit.vout.resize(1);
    txCredit.vin[0].prevout.SetNull();
    txCredit.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    txCredit.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txCredit.vout[0].scriptPubKey = script;
    txCredit.vout[0].nValue = 0;
    MutableTransactionSignatureChecker checker(&txCredit, 0, 0);

    stacktype stack{original_stack};
    ColorIdentifier colorId;
    bool r = EvalScript(stack, script, flags, checker, SigVersion::BASE, &colorId, &err);
    BOOST_CHECK(r);
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
    BOOST_CHECK(stack == expected);
}

BOOST_AUTO_TEST_CASE(checkdatasig_test) {
    // Empty stack.
    CheckError(0, {}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(0, {{0x00}}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(0, {{0x00}, {0x00}}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(0, {}, CScript() << OP_CHECKDATASIGVERIFY,SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(0, {{0x00}}, CScript() << OP_CHECKDATASIGVERIFY,SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(0, {{0x00}, {0x00}}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_INVALID_STACK_OPERATION);

    CheckError(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {}, CScript()<< OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_NOT_MANDATORY_VERIFY_FLAGS,{{0x00}}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {{0x00}, {0x00}}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {}, CScript() << OP_CHECKDATASIGVERIFY,SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {{0x00}}, CScript() << OP_CHECKDATASIGVERIFY,SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {{0x00}, {0x00}}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_INVALID_STACK_OPERATION);

    CheckError(STANDARD_SCRIPT_VERIFY_FLAGS, {}, CScript()<< OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_SCRIPT_VERIFY_FLAGS,{{0x00}}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_SCRIPT_VERIFY_FLAGS, {{0x00}, {0x00}}, CScript() << OP_CHECKDATASIG, SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_SCRIPT_VERIFY_FLAGS, {}, CScript() << OP_CHECKDATASIGVERIFY,SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_SCRIPT_VERIFY_FLAGS, {{0x00}}, CScript() << OP_CHECKDATASIGVERIFY,SCRIPT_ERR_INVALID_STACK_OPERATION);
    CheckError(STANDARD_SCRIPT_VERIFY_FLAGS, {{0x00}, {0x00}}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_INVALID_STACK_OPERATION);

    // Check various pubkey encoding.
    const valtype message{};
    valtype vchHash(32);
    CSHA256().Write(message.data(), message.size()).Finalize(vchHash.data());
    uint256 messageHash(vchHash);

    KeyData kd;
    valtype pubkey = ToByteVector(kd.pubkey);
    valtype pubkeyC = ToByteVector(kd.pubkeyC);
    valtype pubkeyH = ToByteVector(kd.pubkeyH);

    CheckPass(0, {{}, message, pubkey}, CScript() << OP_CHECKDATASIG, {{}});
    CheckPass(0, {{}, message, pubkeyC}, CScript() << OP_CHECKDATASIG, {{}});
    CheckError(0, {{}, message, pubkey}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_CHECKDATASIGVERIFY);
    CheckError(0, {{}, message, pubkeyC}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_CHECKDATASIGVERIFY);

    CheckPass(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {{}, message, pubkey}, CScript() << OP_CHECKDATASIG, {{}});
    CheckPass(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {{}, message, pubkeyC}, CScript() << OP_CHECKDATASIG, {{}});
    CheckError(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {{}, message, pubkey}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_CHECKDATASIGVERIFY);
    CheckError(STANDARD_NOT_MANDATORY_VERIFY_FLAGS, {{}, message, pubkeyC}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_CHECKDATASIGVERIFY);

    CheckPass(STANDARD_SCRIPT_VERIFY_FLAGS, {{}, message, pubkey}, CScript() << OP_CHECKDATASIG, {{}});
    CheckPass(STANDARD_SCRIPT_VERIFY_FLAGS, {{}, message, pubkeyC}, CScript() << OP_CHECKDATASIG, {{}});
    CheckError(STANDARD_SCRIPT_VERIFY_FLAGS, {{}, message, pubkey}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_CHECKDATASIGVERIFY);
    CheckError(STANDARD_SCRIPT_VERIFY_FLAGS, {{}, message, pubkeyC}, CScript() << OP_CHECKDATASIGVERIFY, SCRIPT_ERR_CHECKDATASIGVERIFY);

    // Flags dependent checks.
    const CScript script = CScript() << OP_CHECKDATASIG << OP_NOT << OP_VERIFY;
    const CScript scriptverify = CScript() << OP_CHECKDATASIGVERIFY;

    for(int i:{0,1})
    {
        // Check valid signatures (as in the signature format is valid).
        valtype validsig;
        if(i == 0)
        {
            //sign ECDSA
            kd.privkey.Sign_ECDSA(messageHash, validsig);
            BOOST_CHECK(validsig.size() > 64 && validsig.size() <= 71);
        }
        else
        {
            //sign Schnorr
            kd.privkey.Sign_Schnorr(messageHash, validsig);
            BOOST_CHECK(validsig.size() == 64);
        }
    

        CheckPass(STANDARD_SCRIPT_VERIFY_FLAGS, {validsig, message, pubkey}, CScript() << OP_CHECKDATASIG, {{0x01}});

        CheckPass(STANDARD_SCRIPT_VERIFY_FLAGS, {validsig, message, pubkey}, CScript() << OP_CHECKDATASIGVERIFY, {});

        const valtype minimalsig{0x30, 0x06, 0x02, 0x01, 0x01, 0x02, 0x01, 0x01};
        const valtype nondersig{0x30, 0x80, 0x06, 0x02, 0x01,
                                0x01, 0x02, 0x01, 0x01};
        const valtype highSSig{
            0x30, 0x45, 0x02, 0x20, 0x3e, 0x45, 0x16, 0xda, 0x72, 0x53, 0xcf, 0x06,
            0x8e, 0xff, 0xec, 0x6b, 0x95, 0xc4, 0x12, 0x21, 0xc0, 0xcf, 0x3a, 0x8e,
            0x6c, 0xcb, 0x8c, 0xbf, 0x17, 0x25, 0xb5, 0x62, 0xe9, 0xaf, 0xde, 0x2c,
            0x02, 0x21, 0x00, 0xab, 0x1e, 0x3d, 0xa7, 0x3d, 0x67, 0xe3, 0x20, 0x45,
            0xa2, 0x0e, 0x0b, 0x99, 0x9e, 0x04, 0x99, 0x78, 0xea, 0x8d, 0x6e, 0xe5,
            0x48, 0x0d, 0x48, 0x5f, 0xcf, 0x2c, 0xe0, 0xd0, 0x3b, 0x2e, 0xf0};

        for (uint32_t flags = 1U; flags <= SCRIPT_VERIFY_CONST_SCRIPTCODE; flags<<=1U) {

            // When strict encoding is enforced, hybrid key are invalid.
            CheckError(flags, {{}, message, pubkeyH}, script, SCRIPT_ERR_PUBKEYTYPE);
            CheckError(flags, {{}, message, pubkeyH}, scriptverify, SCRIPT_ERR_PUBKEYTYPE);


            if (flags & SCRIPT_VERIFY_NULLFAIL) {
                // When strict encoding is enforced, hybrid key are invalid.
                CheckError(flags, {minimalsig, message, pubkey}, script, SCRIPT_ERR_SIG_NULLFAIL);
                CheckError(flags, {minimalsig, message, pubkey}, scriptverify, SCRIPT_ERR_SIG_NULLFAIL);

                // Invalid message cause checkdatasig to fail.
                CheckError(flags, {validsig, {0x01}, pubkey}, script, SCRIPT_ERR_SIG_NULLFAIL);
                CheckError(flags, {validsig, {0x01}, pubkey}, scriptverify, SCRIPT_ERR_SIG_NULLFAIL);
            } else {
                // When nullfail is not enforced, invalid signature are just false.
                CheckPass(flags, {minimalsig, message, pubkey}, script, {});
                CheckError(flags, {minimalsig, message, pubkey}, scriptverify, SCRIPT_ERR_CHECKDATASIGVERIFY);

                // Invalid message cause checkdatasig to fail.
                CheckPass(flags, {validsig, {0x01}, pubkey}, script, {});
                CheckError(flags, {validsig, {0x01}, pubkey}, scriptverify, SCRIPT_ERR_CHECKDATASIGVERIFY);
            }

            // If we do enforce low S, then high S sigs are rejected.
            CheckError(flags, {highSSig, message, pubkey}, script, SCRIPT_ERR_SIG_HIGH_S);
            CheckError(flags, {highSSig, message, pubkey}, scriptverify, SCRIPT_ERR_SIG_HIGH_S);

            
            // If we get any of the dersig flags, the non canonical dersig
            // signature fails.
            CheckError(flags, {nondersig, message, pubkey}, script, SCRIPT_ERR_SIG_DER);
            CheckError(flags, {nondersig, message, pubkey}, scriptverify, SCRIPT_ERR_SIG_DER);

        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
