// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/data/script_tests.json.h>

#include <core_io.h>
#include <key.h>
#include <keystore.h>
#include <policy/policy.h>
#include <rpc/server.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/standard.h>
#include <script/sign.h>
#include <test/test_tapyrus.h>
#include <util.h>
#include <utilstrencodings.h>
#include <coloridentifier.h>

#if defined(HAVE_CONSENSUS_LIB)
#include <script/tapyrusconsensus.h>
#endif

#include <fstream>
#include <stdint.h>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

// Uncomment if you want to output updated JSON tests.
// #define UPDATE_JSON_TESTS

static const unsigned int stdFlags = STANDARD_SCRIPT_VERIFY_FLAGS;
static const unsigned int mndFlags = STANDARD_NOT_MANDATORY_VERIFY_FLAGS;

unsigned int ParseScriptFlags(std::string strFlags);
std::string FormatScriptFlags(unsigned int flags);

UniValue
read_json(const std::string& jsondata)
{
    UniValue v;

    if (!v.read(jsondata) || !v.isArray()) {
        BOOST_ERROR("Parse error.");
        return UniValue(UniValue::VARR);
    }
    return v.get_array();
}

struct ScriptErrorDesc {
    ScriptError_t err;
    const char* name;
};

static ScriptErrorDesc script_errors[] = {
    {SCRIPT_ERR_OK, "OK"},
    {SCRIPT_ERR_UNKNOWN_ERROR, "UNKNOWN_ERROR"},
    {SCRIPT_ERR_EVAL_FALSE, "EVAL_FALSE"},
    {SCRIPT_ERR_OP_RETURN, "OP_RETURN"},
    {SCRIPT_ERR_SCRIPT_SIZE, "SCRIPT_SIZE"},
    {SCRIPT_ERR_PUSH_SIZE, "PUSH_SIZE"},
    {SCRIPT_ERR_OP_COUNT, "OP_COUNT"},
    {SCRIPT_ERR_STACK_SIZE, "STACK_SIZE"},
    {SCRIPT_ERR_SIG_COUNT, "SIG_COUNT"},
    {SCRIPT_ERR_PUBKEY_COUNT, "PUBKEY_COUNT"},
    {SCRIPT_ERR_VERIFY, "VERIFY"},
    {SCRIPT_ERR_EQUALVERIFY, "EQUALVERIFY"},
    {SCRIPT_ERR_CHECKMULTISIGVERIFY, "CHECKMULTISIGVERIFY"},
    {SCRIPT_ERR_CHECKSIGVERIFY, "CHECKSIGVERIFY"},
    {SCRIPT_ERR_NUMEQUALVERIFY, "NUMEQUALVERIFY"},
    {SCRIPT_ERR_CHECKDATASIGVERIFY, "CHECKDATASIGVERIFY"},
    {SCRIPT_ERR_BAD_OPCODE, "BAD_OPCODE"},
    {SCRIPT_ERR_DISABLED_OPCODE, "DISABLED_OPCODE"},
    {SCRIPT_ERR_INVALID_STACK_OPERATION, "INVALID_STACK_OPERATION"},
    {SCRIPT_ERR_INVALID_ALTSTACK_OPERATION, "INVALID_ALTSTACK_OPERATION"},
    {SCRIPT_ERR_UNBALANCED_CONDITIONAL, "UNBALANCED_CONDITIONAL"},
    {SCRIPT_ERR_MIXED_SCHEME_MULTISIG, "MIXED_SCHEME_MULTISIG"},
    {SCRIPT_ERR_NEGATIVE_LOCKTIME, "NEGATIVE_LOCKTIME"},
    {SCRIPT_ERR_UNSATISFIED_LOCKTIME, "UNSATISFIED_LOCKTIME"},
    {SCRIPT_ERR_SIG_HASHTYPE, "SIG_HASHTYPE"},
    {SCRIPT_ERR_SIG_DER, "SIG_DER"},
    {SCRIPT_ERR_MINIMALDATA, "MINIMALDATA"},
    {SCRIPT_ERR_SIG_PUSHONLY, "SIG_PUSHONLY"},
    {SCRIPT_ERR_SIG_HIGH_S, "SIG_HIGH_S"},
    {SCRIPT_ERR_SIG_NULLDUMMY, "SIG_NULLDUMMY"},
    {SCRIPT_ERR_PUBKEYTYPE, "PUBKEYTYPE"},
    {SCRIPT_ERR_CLEANSTACK, "CLEANSTACK"},
    {SCRIPT_ERR_MINIMALIF, "MINIMALIF"},
    {SCRIPT_ERR_SIG_NULLFAIL, "NULLFAIL"},
    {SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS, "DISCOURAGE_UPGRADABLE_NOPS"},
    {SCRIPT_ERR_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM, "DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM"},
    {SCRIPT_ERR_WITNESS_PROGRAM_WRONG_LENGTH, "WITNESS_PROGRAM_WRONG_LENGTH"},
    {SCRIPT_ERR_WITNESS_PROGRAM_WITNESS_EMPTY, "WITNESS_PROGRAM_WITNESS_EMPTY"},
    {SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH, "WITNESS_PROGRAM_MISMATCH"},
    {SCRIPT_ERR_WITNESS_MALLEATED, "WITNESS_MALLEATED"},
    {SCRIPT_ERR_WITNESS_MALLEATED_P2SH, "WITNESS_MALLEATED_P2SH"},
    {SCRIPT_ERR_WITNESS_UNEXPECTED, "WITNESS_UNEXPECTED"},
    {SCRIPT_ERR_WITNESS_PUBKEYTYPE, "WITNESS_PUBKEYTYPE"},
    {SCRIPT_ERR_OP_CODESEPARATOR, "OP_CODESEPARATOR"},
    {SCRIPT_ERR_SIG_FINDANDDELETE, "SIG_FINDANDDELETE"},
    {SCRIPT_ERR_OP_COLOR_UNEXPECTED, "OP_COLOR_UNEXPECTED"},
    {SCRIPT_ERR_OP_COLORID_INVALID, "OP_COLORID_INVALID"},
    {SCRIPT_ERR_OP_COLORMULTIPLE, "MULTIPLE_COLORID"},
    {SCRIPT_ERR_OP_COLORINBRANCH, "COLOR_IN_BRANCH"}

};

static const char* FormatScriptError(ScriptError_t err)
{
    for (unsigned int i = 0; i < ARRAYLEN(script_errors); ++i)
        if (script_errors[i].err == err)
            return script_errors[i].name;
    BOOST_ERROR("Unknown scripterror enumeration value, update script_errors in script_tests.cpp.");
    return "";
}

static ScriptError_t ParseScriptError(const std::string& name)
{
    for (unsigned int i = 0; i < ARRAYLEN(script_errors); ++i)
        if (script_errors[i].name == name)
            return script_errors[i].err;
    BOOST_ERROR("Unknown scripterror \"" << name << "\" in test description");
    return SCRIPT_ERR_UNKNOWN_ERROR;
}

BOOST_FIXTURE_TEST_SUITE(script_tests, BasicTestingSetup)

CMutableTransaction BuildCreditingTransaction(const CScript& scriptPubKey, int nValue = 0)
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

CMutableTransaction BuildSpendingTransaction(const CScript& scriptSig, const CScriptWitness& scriptWitness, const CTransaction& txCredit)
{
    CMutableTransaction txSpend;
    txSpend.nFeatures = 1;
    txSpend.nLockTime = 0;
    txSpend.vin.resize(1);
    txSpend.vout.resize(1);
    txSpend.vin[0].scriptWitness = scriptWitness;
    txSpend.vin[0].prevout.hashMalFix = txCredit.GetHashMalFix();
    txSpend.vin[0].prevout.n = 0;
    txSpend.vin[0].scriptSig = scriptSig;
    txSpend.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txSpend.vout[0].scriptPubKey = CScript();
    txSpend.vout[0].nValue = txCredit.vout[0].nValue;

    return txSpend;
}

void DoTest(const CScript& scriptPubKey, const CScript& scriptSig, const CScriptWitness& scriptWitness, int flags, const std::string& message, int scriptError, CAmount nValue = 0)
{
    bool expect = (scriptError == SCRIPT_ERR_OK);
    if (flags & SCRIPT_VERIFY_CLEANSTACK) {
        flags |= SCRIPT_VERIFY_WITNESS;
    }
    ScriptError err;
    ColorIdentifier colorId;
    const CTransaction txCredit{BuildCreditingTransaction(scriptPubKey, nValue)};
    CMutableTransaction tx = BuildSpendingTransaction(scriptSig, scriptWitness, txCredit);
    CMutableTransaction tx2 = tx;
    BOOST_CHECK_MESSAGE(VerifyScript(scriptSig, scriptPubKey, &scriptWitness, flags, MutableTransactionSignatureChecker(&tx, 0, txCredit.vout[0].nValue), colorId, &err) == expect, message);
    BOOST_CHECK_MESSAGE(err == scriptError, std::string(FormatScriptError(err)) + " where " + std::string(FormatScriptError((ScriptError_t)scriptError)) + " expected: " + message);

    // Verify that removing flags from a passing test or adding flags to a failing test does not change the result.
    constexpr unsigned int test_flags_list[] = {SCRIPT_VERIFY_NONE,
        SCRIPT_VERIFY_SIGPUSHONLY,
        SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS,
        SCRIPT_VERIFY_CLEANSTACK,
        //SCRIPT_VERIFY_WITNESS,
        SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM,
        SCRIPT_VERIFY_MINIMALIF,
        SCRIPT_VERIFY_NULLFAIL,
        SCRIPT_VERIFY_WITNESS_PUBKEYTYPE,
        SCRIPT_VERIFY_CONST_SCRIPTCODE};
    // If we add many more flags, this loop can get too expensive, but we can
    // rewrite in the future to randomly pick a set of flags to evaluate.
    for (auto extra_flags: test_flags_list) {
        int combined_flags = expect ? (flags & ~extra_flags) : (flags | extra_flags);
        // Weed out some invalid flag combinations.
        //if (combined_flags & SCRIPT_VERIFY_CLEANSTACK && ~combined_flags & ( SCRIPT_VERIFY_WITNESS)) continue;
        colorId = ColorIdentifier();

        BOOST_CHECK_MESSAGE(VerifyScript(scriptSig, scriptPubKey, &scriptWitness, combined_flags, MutableTransactionSignatureChecker(&tx, 0, txCredit.vout[0].nValue), colorId, &err) == expect, message + strprintf(" (with flags %x)", combined_flags));
    }

#if defined(HAVE_CONSENSUS_LIB)
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx2;
    int libconsensus_flags = flags & bitcoinconsensus_SCRIPT_FLAGS_VERIFY_ALL;
    if (libconsensus_flags == flags) {
        BOOST_CHECK_MESSAGE(bitcoinconsensus_verify_script_with_amount(scriptPubKey.data(), scriptPubKey.size(), 0, (const unsigned char*)&stream[0], stream.size(), 0, libconsensus_flags, nullptr) == expect, message);
        BOOST_CHECK_MESSAGE(bitcoinconsensus_verify_script(scriptPubKey.data(), scriptPubKey.size(), (const unsigned char*)&stream[0], stream.size(), 0, libconsensus_flags, nullptr) == expect, message);
    }
#endif
}

void static NegateSignatureS(std::vector<unsigned char>& vchSig)
{
    // Parse the signature.
    std::vector<unsigned char> r, s;
    r = std::vector<unsigned char>(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
    s = std::vector<unsigned char>(vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);

    // Really ugly to implement mod-n negation here, but it would be feature creep to expose such functionality from libsecp256k1.
    static const unsigned char order[33] = {
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
        0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
        0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};
    while (s.size() < 33) {
        s.insert(s.begin(), 0x00);
    }
    int carry = 0;
    for (int p = 32; p >= 1; p--) {
        int n = (int)order[p] - s[p] - carry;
        s[p] = (n + 256) & 0xFF;
        carry = (n < 0);
    }
    assert(carry == 0);
    if (s.size() > 1 && s[0] == 0 && s[1] < 0x80) {
        s.erase(s.begin());
    }

    // Reconstruct the signature.
    vchSig.clear();
    vchSig.push_back(0x30);
    vchSig.push_back(4 + r.size() + s.size());
    vchSig.push_back(0x02);
    vchSig.push_back(r.size());
    vchSig.insert(vchSig.end(), r.begin(), r.end());
    vchSig.push_back(0x02);
    vchSig.push_back(s.size());
    vchSig.insert(vchSig.end(), s.begin(), s.end());
}

namespace {
const unsigned char vchKey0[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
const unsigned char vchKey1[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
const unsigned char vchKey2[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0};

struct KeyData {
    CKey key0, key0C, key1, key1C, key2, key2C;
    CPubKey pubkey0, pubkey0C, pubkey0H;
    CPubKey pubkey1, pubkey1C;
    CPubKey pubkey2, pubkey2C;

    KeyData()
    {
        key0.Set(vchKey0, vchKey0 + 32, false);
        key0C.Set(vchKey0, vchKey0 + 32, true);
        pubkey0 = key0.GetPubKey();
        pubkey0H = key0.GetPubKey();
        pubkey0C = key0C.GetPubKey();
        *const_cast<unsigned char*>(&pubkey0H[0]) = 0x06 | (pubkey0H[64] & 1);

        key1.Set(vchKey1, vchKey1 + 32, false);
        key1C.Set(vchKey1, vchKey1 + 32, true);
        pubkey1 = key1.GetPubKey();
        pubkey1C = key1C.GetPubKey();

        key2.Set(vchKey2, vchKey2 + 32, false);
        key2C.Set(vchKey2, vchKey2 + 32, true);
        pubkey2 = key2.GetPubKey();
        pubkey2C = key2C.GetPubKey();
    }
};

enum class WitnessMode {
    NONE,
    PKH,
    SH
};

class TestBuilder
{
private:
    //! Actually executed script
    CScript script;
    //! The P2SH redeemscript
    CScript redeemscript;
    //! The Witness embedded script
    CScript witscript;
    CScriptWitness scriptWitness;
    CTransactionRef creditTx;
    CMutableTransaction spendTx;
    bool havePush;
    std::vector<unsigned char> push;
    std::string comment;
    int flags;
    int scriptError;
    CAmount nValue;

    void DoPush()
    {
        if (havePush) {
            spendTx.vin[0].scriptSig << push;
            havePush = false;
        }
    }

    void DoPush(const std::vector<unsigned char>& data)
    {
        DoPush();
        push = data;
        havePush = true;
    }


public:
    TestBuilder(const CScript& script_, const std::string& comment_, int flags_, bool P2SH = false, WitnessMode wm = WitnessMode::NONE, int witnessversion = 0, CAmount nValue_ = 0, bool ignoreColor = false) : script(script_), havePush(false), comment(comment_), flags(flags_), scriptError(SCRIPT_ERR_OK), nValue(nValue_)
    {
        CScript scriptPubKey = script;
        if (wm == WitnessMode::PKH) {
            uint160 hash;
            CHash160().Write(&script[1], script.size() - 1).Finalize(hash.begin());
            script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(hash) << OP_EQUALVERIFY << OP_CHECKSIG;
            scriptPubKey = CScript() << witnessversion << ToByteVector(hash);
        } else if (wm == WitnessMode::SH) {
            witscript = scriptPubKey;
            uint256 hash;
            CSHA256().Write(&witscript[0], witscript.size()).Finalize(hash.begin());
            scriptPubKey = CScript() << witnessversion << ToByteVector(hash);
        }
        if (P2SH) {
            if(!ignoreColor && scriptPubKey.IsColoredScript())
            {
                const KeyData keys;
                redeemscript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
            }
            else
            {
                redeemscript = scriptPubKey;
                scriptPubKey = CScript() << OP_HASH160 << ToByteVector(CScriptID(redeemscript)) << OP_EQUAL;
            }
        }
        creditTx = MakeTransactionRef(BuildCreditingTransaction(scriptPubKey, nValue));
        spendTx = BuildSpendingTransaction(CScript(), CScriptWitness(), *creditTx);
    }

    TestBuilder& ScriptError(ScriptError_t err)
    {
        scriptError = err;
        return *this;
    }

    TestBuilder& Add(const CScript& _script)
    {
        DoPush();
        spendTx.vin[0].scriptSig += _script;
        return *this;
    }

    TestBuilder& Num(int num)
    {
        DoPush();
        spendTx.vin[0].scriptSig << num;
        return *this;
    }

    TestBuilder& Push(const std::string& hex)
    {
        DoPush(ParseHex(hex));
        return *this;
    }

    TestBuilder& Push(const CScript& _script)
    {
        DoPush(std::vector<unsigned char>(_script.begin(), _script.end()));
        return *this;
    }

    TestBuilder& PushSig(const CKey& key, SignatureScheme scheme, int nHashType = SIGHASH_ALL, unsigned int lenR = 32, unsigned int lenS = 32, SigVersion sigversion = SigVersion::BASE, CAmount amount = 0)
    {
        uint256 hash = SignatureHash(script, spendTx, 0, nHashType, amount, sigversion);

        std::vector<unsigned char> vchSig, r, s;
        uint32_t iter = 0;
        do {
            if(scheme == SignatureScheme::ECDSA)
            {
                key.Sign_ECDSA(hash, vchSig, false, iter++);
                
                if ((lenS == 33) != (vchSig[5 + vchSig[3]] == 33)) 
                    NegateSignatureS(vchSig);

                r = std::vector<unsigned char>(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
                s = std::vector<unsigned char>(vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);
            }
            else
            {
                key.Sign_Schnorr(hash, vchSig, iter++);

                r = std::vector<unsigned char>(vchSig.begin(), vchSig.begin() + 32);
                s = std::vector<unsigned char>(vchSig.begin() + 32, vchSig.begin() + 64);
            }

        } while (lenR != r.size() || lenS != s.size());

        vchSig.push_back(static_cast<unsigned char>(nHashType));
        DoPush(vchSig);
        return *this;
    }

    TestBuilder& PushDataSig(const CKey& key, SignatureScheme scheme, const std::vector<uint8_t>& data, unsigned int lenR = 32, unsigned int lenS = 32)
    {
        std::vector<unsigned char> vchHash(32);
        CSHA256().Write(data.data(), data.size()).Finalize(vchHash.data());

        uint256 messageHash(vchHash);
        ;
        std::vector<uint8_t> vchSig, r, s;
        uint32_t iter = 0;
        do {
            if(scheme == SignatureScheme::ECDSA)
            {
                key.Sign_ECDSA(messageHash, vchSig, iter++);
                if ((lenS == 33) != (vchSig[5 + vchSig[3]] == 33))
                    NegateSignatureS(vchSig);
                
                r = std::vector<uint8_t>(vchSig.begin() + 4,
                vchSig.begin() + 4 + vchSig[3]);
                s = std::vector<uint8_t>(vchSig.begin() + 6 + vchSig[3],
                vchSig.begin() + 6 + vchSig[3] +
                    vchSig[5 + vchSig[3]]);
            }
            else
            {
                key.Sign_Schnorr(messageHash, vchSig, iter++);

                r = std::vector<uint8_t>(vchSig.begin(),
                    vchSig.begin() + 32);
                s = std::vector<uint8_t>(vchSig.begin() + 32,
                    vchSig.begin() + 64);
            }

        } while (lenR != r.size() || lenS != s.size());

        DoPush(vchSig);
        return *this;
    }

    TestBuilder& PushWitSig(const CKey& key, CAmount amount = -1, int nHashType = SIGHASH_ALL, unsigned int lenR = 32, unsigned int lenS = 32, SigVersion sigversion = SigVersion::WITNESS_V0)
    {
        if (amount == -1)
            amount = nValue;
        return PushSig(key, SignatureScheme::ECDSA, nHashType, lenR, lenS, sigversion, amount).AsWit();
    }

    TestBuilder& Push(const CPubKey& pubkey)
    {
        DoPush(std::vector<unsigned char>(pubkey.begin(), pubkey.end()));
        return *this;
    }

    TestBuilder& PushRedeem()
    {
        DoPush(std::vector<unsigned char>(redeemscript.begin(), redeemscript.end()));
        return *this;
    }

    TestBuilder& PushWitRedeem()
    {
        DoPush(std::vector<unsigned char>(witscript.begin(), witscript.end()));
        return AsWit();
    }

    TestBuilder& EditPush(unsigned int pos, const std::string& hexin, const std::string& hexout)
    {
        assert(havePush);
        std::vector<unsigned char> datain = ParseHex(hexin);
        std::vector<unsigned char> dataout = ParseHex(hexout);
        assert(pos + datain.size() <= push.size());
        BOOST_CHECK_MESSAGE(std::vector<unsigned char>(push.begin() + pos, push.begin() + pos + datain.size()) == datain, comment);
        push.erase(push.begin() + pos, push.begin() + pos + datain.size());
        push.insert(push.begin() + pos, dataout.begin(), dataout.end());
        return *this;
    }

    TestBuilder& DamagePush(unsigned int pos)
    {
        assert(havePush);
        assert(pos < push.size());
        push[pos] ^= 1;
        return *this;
    }

    TestBuilder& Test()
    {
        TestBuilder copy = *this; // Make a copy so we can rollback the push.
        DoPush();
        DoTest(creditTx->vout[0].scriptPubKey, spendTx.vin[0].scriptSig, scriptWitness, flags, comment, scriptError, nValue);
        *this = copy;
        return *this;
    }

    TestBuilder& AsWit()
    {
        assert(havePush);
        scriptWitness.stack.push_back(push);
        havePush = false;
        return *this;
    }

    UniValue GetJSON()
    {
        DoPush();
        UniValue array(UniValue::VARR);
        if (!scriptWitness.stack.empty()) {
            UniValue wit(UniValue::VARR);
            for (unsigned i = 0; i < scriptWitness.stack.size(); i++) {
                wit.push_back(HexStr(scriptWitness.stack[i]));
            }
            wit.push_back(ValueFromAmount(nValue));
            array.push_back(wit);
        }
        array.push_back(FormatScript(spendTx.vin[0].scriptSig));
        array.push_back(FormatScript(creditTx->vout[0].scriptPubKey));
        array.push_back(FormatScriptFlags(flags));
        array.push_back(FormatScriptError((ScriptError_t)scriptError));
        array.push_back(comment);
        return array;
    }

    std::string GetComment() const
    {
        return comment;
    }
};

std::string JSONPrettyPrint(const UniValue& univalue)
{
    std::string ret = univalue.write(4);
    // Workaround for libunivalue pretty printer, which puts a space between commas and newlines
    size_t pos = 0;
    while ((pos = ret.find(" \n", pos)) != std::string::npos) {
        ret.replace(pos, 2, "\n");
        pos++;
    }
    return ret;
}
} // namespace

BOOST_AUTO_TEST_CASE(script_build)
{
    const KeyData keys;

    std::vector<TestBuilder> tests;

    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK ECDSA", 0)
                        .PushSig(keys.key0, SignatureScheme::ECDSA));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK SCHNORR", 0)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK ECDSA, bad sig", 0)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK SCHNORR, bad sig", 0)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2PKH ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2PKH SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey2C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2PKH ECDSA, bad pubkey", 0)
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .Push(keys.pubkey2C)
                        .DamagePush(5)
                        .ScriptError(SCRIPT_ERR_EQUALVERIFY));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey2C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2PKH SCHNORR, bad pubkey", 0)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey2C)
                        .DamagePush(5)
                        .ScriptError(SCRIPT_ERR_EQUALVERIFY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "P2PK ECDSA anyonecanpay", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL | SIGHASH_ANYONECANPAY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "P2PK SCHNORR anyonecanpay", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR, SIGHASH_ALL | SIGHASH_ANYONECANPAY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "P2PK ECDSA anyonecanpay marked with normal hashtype", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL | SIGHASH_ANYONECANPAY)
                        .EditPush(70, "81", "01")
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "P2PK SCHNORR anyonecanpay marked with normal hashtype", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR, SIGHASH_ALL | SIGHASH_ANYONECANPAY)
                        .EditPush(64, "81", "01")
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
        "P2SH(P2PK) ECDSA", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
        "P2SH(P2PK) SCHNORR", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
        "P2SH(P2PK) ECDSA, bad redeemscript", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushRedeem()
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
        "P2SH(P2PK) SCHNORR, bad redeemscript", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushRedeem()
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey0.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2SH(P2PKH) ECDSA", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .Push(keys.pubkey0)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey0.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2SH(P2PKH) SCHNORR", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey0)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2SH(P2PKH) ECDSA, bad sig", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .DamagePush(10)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_EQUALVERIFY));
    tests.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2SH(P2PKH) SCHNORR, bad sig", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .DamagePush(10)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_EQUALVERIFY));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 ECDSA", 0)
                        .Num(0)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .PushSig(keys.key2, SignatureScheme::ECDSA));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 SCHNORR", 0)
                        .Num(0)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 MIXED 1", 0)
                        .Num(0)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .ScriptError(SCRIPT_ERR_MIXED_SCHEME_MULTISIG));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 MIXED 2", 0)
                        .Num(0)
                        .PushSig(keys.key0C, SignatureScheme::SCHNORR)
                        .PushSig(keys.key1C, SignatureScheme::ECDSA)
                        .PushSig(keys.key2C, SignatureScheme::SCHNORR)
                        .ScriptError(SCRIPT_ERR_MIXED_SCHEME_MULTISIG));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 ECDSA, 2 sigs", 0)
                        .Num(0)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 SCHNORR, 2 sigs", 0)
                        .Num(0)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 MIXED 1, 2 sigs both incorrect", 0)
                        .Num(0)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 MIXED 2, 2 sigs both correct", 0)
                        .Num(0)
                        .PushSig(keys.key0C, SignatureScheme::SCHNORR)
                        .PushSig(keys.key1C, SignatureScheme::ECDSA)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "P2SH(2-of-3) ECDSA", SCRIPT_VERIFY_NONE, true)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "P2SH(2-of-3) SCHNORR", SCRIPT_VERIFY_NONE, true)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "P2SH(2-of-3) MIXED", SCRIPT_VERIFY_NONE, true)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_MIXED_SCHEME_MULTISIG));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "P2SH(2-of-3) ECDSA, 1 sig", SCRIPT_VERIFY_NONE, true)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Num(0)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "P2SH(2-of-3) SCHNORR, 1 sig", SCRIPT_VERIFY_NONE, true)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Num(0)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "P2PK ECDSA with too much R padding", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "P2PK ECDSA with too much S padding", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL)
                        .EditPush(1, "44", "45")
                        .EditPush(37, "20", "2100")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "P2PK SCHNORR with too much padding interpretes as ECDSA", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR, SIGHASH_ALL)
                        .EditPush(0, "8e", "8e00")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "P2PK ECDSA with too little R padding", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
        "P2PK ECDSA NOT with bad sig with too much R padding", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key2, SignatureScheme::ECDSA, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000")
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
        "P2PK ECDSA NOT with too much R padding", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key2, SignatureScheme::ECDSA, SIGHASH_ALL, 31, 32)
                        .EditPush(1, "43021F", "44022000")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "BIP66 example 1, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
        "BIP66 example 2, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "BIP66 example 3, with DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
        "BIP66 example 4, with DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "BIP66 example 5, with DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(1)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
        "BIP66 example 6, with DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(1)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
        "BIP66 example 7, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
        "BIP66 example 8, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
        "BIP66 example 9, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .Num(0)
                        .PushSig(keys.key2, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
        "BIP66 example 10, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .Num(0)
                        .PushSig(keys.key2, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
        "BIP66 example 11, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
        "BIP66 example 12, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2PK with multi-byte hashtype, with ECDSA DERSIG", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key2, SignatureScheme::ECDSA, SIGHASH_ALL)
                        .EditPush(70, "01", "0101")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2PK with multi-byte hashtype, with SCHNORR SIG", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR, SIGHASH_ALL)
                        .EditPush(64, "01", "0101")
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2PK ECDSA with high S", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key2, SignatureScheme::ECDSA, SIGHASH_ALL, 32, 33)
                        .ScriptError(SCRIPT_ERR_SIG_HIGH_S));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
        "P2PK ECDSA with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key0, SignatureScheme::ECDSA, SIGHASH_ALL)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
        "P2PK SCHNORR with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR, SIGHASH_ALL)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
        "P2PK ECDSA NOT with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key0, SignatureScheme::ECDSA, SIGHASH_ALL)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
        "P2PK SCHNORR NOT with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR, SIGHASH_ALL)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
        "P2PK ECDSA NOT with invalid hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key0, SignatureScheme::ECDSA, SIGHASH_ALL)
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
        "P2PK SCHNORR NOT with invalid hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR, SIGHASH_ALL)
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
        "1-of-2 ECDSA with the second 1 hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
        "1-of-2 SCHNORR with the second 1 hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR, SIGHASH_ALL));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0H) << OP_2 << OP_CHECKMULTISIG,
        "1-of-2 ECDSA with the first 1 hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, SIGHASH_ALL)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0H) << OP_2 << OP_CHECKMULTISIG,
        "1-of-2 SCHNORR with the first 1 hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR, SIGHASH_ALL)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "P2PK ECDSA with undefined hashtype", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, 5)
                        .ScriptError(SCRIPT_ERR_SIG_HASHTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "P2PK SCHNORR with undefined hashtype", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR, 5)
                        .ScriptError(SCRIPT_ERR_SIG_HASHTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
        "P2PK ECDSA NOT with invalid sig and undefined hashtype", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, 5)
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_SIG_HASHTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
        "P2PK SCHNORR NOT with invalid sig and undefined hashtype", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR, 5)
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_SIG_HASHTYPE));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 ECDSA with nonzero dummy", SCRIPT_VERIFY_NONE)
                        .Num(1)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .ScriptError(SCRIPT_ERR_SIG_NULLDUMMY));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
        "3-of-3 SCHNORR with nonzero dummy", SCRIPT_VERIFY_NONE)
                        .Num(1)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .ScriptError(SCRIPT_ERR_SIG_NULLDUMMY));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
        "3-of-3 ECDSA NOT with invalid sig with nonzero dummy", SCRIPT_VERIFY_NONE)
                        .Num(1)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_SIG_NULLDUMMY));
    tests.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
        "3-of-3 SCHNORR NOT with invalid sig with nonzero dummy", SCRIPT_VERIFY_NONE)
                        .Num(1)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .DamagePush(10)
                        .ScriptError(SCRIPT_ERR_SIG_NULLDUMMY));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
        "2-of-2 ECDSA with two identical keys and sigs pushed using OP_DUP", SCRIPT_VERIFY_SIGPUSHONLY)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Add(CScript() << OP_DUP)
                        .ScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
        "2-of-2 SCHNORR with two identical keys and sigs pushed using OP_DUP", SCRIPT_VERIFY_SIGPUSHONLY)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Add(CScript() << OP_DUP)
                        .ScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2SH(P2PK) ECDSA with non-push scriptSig but no P2SH or SIGPUSHONLY", 0, true)
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2SH(P2PK) SCHNORR with non-push scriptSig but no P2SH or SIGPUSHONLY", 0, true)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2PK ECDSA with non-push scriptSig but with P2SH validation", 0)
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .Add(CScript() << OP_NOP8));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2PK SCHNORR with non-push scriptSig but with P2SH validation", 0)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .Add(CScript() << OP_NOP8));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2SH(P2PK) ECDSA with non-push scriptSig but no SIGPUSHONLY", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2SH(P2PK) SCHNORR with non-push scriptSig but no SIGPUSHONLY", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2SH(P2PK) ECDSA with non-push scriptSig but not P2SH", SCRIPT_VERIFY_SIGPUSHONLY, true)
                        .PushSig(keys.key2, SignatureScheme::ECDSA)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
        "P2SH(P2PK) SCHNORR with non-push scriptSig but not P2SH", SCRIPT_VERIFY_SIGPUSHONLY, true)
                        .PushSig(keys.key2, SignatureScheme::SCHNORR)
                        .Add(CScript() << OP_NOP8)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_SIG_PUSHONLY));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
        "2-of-2 ECDSA with two identical keys and sigs pushed", SCRIPT_VERIFY_SIGPUSHONLY)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .PushSig(keys.key1, SignatureScheme::ECDSA));
    tests.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
        "2-of-2 SCHNORR with two identical keys and sigs pushed", SCRIPT_VERIFY_SIGPUSHONLY)
                        .Num(0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK ECDSA with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_NONE)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::ECDSA));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK SCHNORR with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_NONE)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK ECDSA with unnecessary input", SCRIPT_VERIFY_CLEANSTACK)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .ScriptError(SCRIPT_ERR_CLEANSTACK));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK SCHNORR with unnecessary input", SCRIPT_VERIFY_CLEANSTACK)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .ScriptError(SCRIPT_ERR_CLEANSTACK));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2SH ECDSA with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_NONE, true)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2SH SCHNORR with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_NONE, true)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2SH ECDSA with unnecessary input", SCRIPT_VERIFY_CLEANSTACK, true)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_CLEANSTACK));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2SH SCHNORR with unnecessary input", SCRIPT_VERIFY_CLEANSTACK, true)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_CLEANSTACK));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2SH ECDSA with unnecessary input", SCRIPT_VERIFY_CLEANSTACK, true)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_CLEANSTACK));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2SH SCHNORR with unnecessary input", SCRIPT_VERIFY_CLEANSTACK, true)
                        .Num(11)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_CLEANSTACK));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2SH ECDSA with CLEANSTACK", SCRIPT_VERIFY_CLEANSTACK, true)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2SH SCHNORR with CLEANSTACK", SCRIPT_VERIFY_CLEANSTACK, true)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "Basic P2WSH", SCRIPT_VERIFY_WITNESS, false, WitnessMode::SH,
        0, 1)
                        .PushWitSig(keys.key0)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "Basic P2WPKH", SCRIPT_VERIFY_WITNESS, false, WitnessMode::PKH,
        0, 1)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey0)
                        .AsWit());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "Basic P2SH(P2WSH)", SCRIPT_VERIFY_WITNESS, true, WitnessMode::SH,
        0, 1)
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "Basic P2SH(P2WPKH)", SCRIPT_VERIFY_WITNESS, true, WitnessMode::PKH,
        0, 1)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey0)
                        .AsWit()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "Basic P2WSH with the wrong key", SCRIPT_VERIFY_WITNESS, false, WitnessMode::SH)
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1),
        "Basic P2WPKH with the wrong key", SCRIPT_VERIFY_WITNESS, false, WitnessMode::PKH)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey1)
                        .AsWit()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "Basic P2SH(P2WSH) with the wrong key", SCRIPT_VERIFY_WITNESS, true, WitnessMode::SH)
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1),
        "Basic P2SH(P2WPKH) with the wrong key", SCRIPT_VERIFY_WITNESS, true, WitnessMode::PKH)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey1)
                        .AsWit()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "Basic P2WSH with the wrong key but no WITNESS", SCRIPT_VERIFY_NONE, false, WitnessMode::SH)
                        .PushWitSig(keys.key0)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1),
        "Basic P2WPKH with the wrong key but no WITNESS", SCRIPT_VERIFY_NONE, false, WitnessMode::PKH)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey1)
                        .AsWit());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
        "Basic P2SH(P2WSH) with the wrong key but no WITNESS", SCRIPT_VERIFY_NONE, true, WitnessMode::SH)
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1),
        "Basic P2SH(P2WPKH) with the wrong key but no WITNESS", SCRIPT_VERIFY_NONE, true, WitnessMode::PKH)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey1)
                        .AsWit()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "Basic P2WSH with wrong value", SCRIPT_VERIFY_WITNESS, false, WitnessMode::SH,
        0, 0)
                        .PushWitSig(keys.key0, 1)
                        .PushWitRedeem()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "Basic P2WPKH with wrong value", SCRIPT_VERIFY_WITNESS, false, WitnessMode::PKH,
        0, 0)
                        .PushWitSig(keys.key0, 1)
                        .Push(keys.pubkey0)
                        .AsWit()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "Basic P2SH(P2WSH) with wrong value", SCRIPT_VERIFY_WITNESS, true, WitnessMode::SH,
        0, 0)
                        .PushWitSig(keys.key0, 1)
                        .PushWitRedeem()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "Basic P2SH(P2WPKH) with wrong value",  SCRIPT_VERIFY_WITNESS, true, WitnessMode::PKH,
        0, 0)
                        .PushWitSig(keys.key0, 1)
                        .Push(keys.pubkey0)
                        .AsWit()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "P2WPKH with future witness version",  SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM|SCRIPT_VERIFY_WITNESS, false, WitnessMode::PKH, 1)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey0)
                        .AsWit()
                        .ScriptError(SCRIPT_ERR_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM));
    {
        CScript witscript = CScript() << ToByteVector(keys.pubkey0);
        uint256 hash;
        CSHA256().Write(&witscript[0], witscript.size()).Finalize(hash.begin());
        std::vector<unsigned char> hashBytes = ToByteVector(hash);
        hashBytes.pop_back();
        tests.push_back(TestBuilder(CScript() << OP_0 << hashBytes,
            "P2WPKH with wrong witness program length", SCRIPT_VERIFY_WITNESS, false)
                            .PushWitSig(keys.key0)
                            .Push(keys.pubkey0)
                            .AsWit()
                            .ScriptError(SCRIPT_ERR_WITNESS_PROGRAM_WRONG_LENGTH));
    }
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2WSH with empty witness", SCRIPT_VERIFY_WITNESS, false, WitnessMode::SH)
                        .ScriptError(SCRIPT_ERR_WITNESS_PROGRAM_WITNESS_EMPTY));
    {
        CScript witscript = CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG;
        tests.push_back(TestBuilder(witscript,
            "P2WSH with witness program mismatch", SCRIPT_VERIFY_WITNESS, false, WitnessMode::SH)
                            .PushWitSig(keys.key0)
                            .Push(witscript)
                            .DamagePush(0)
                            .AsWit()
                            .ScriptError(SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH));
    }
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "P2WPKH with witness program mismatch", SCRIPT_VERIFY_WITNESS, false, WitnessMode::PKH)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey0)
                        .AsWit()
                        .Push("0")
                        .AsWit()
                        .ScriptError(SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "P2WPKH with non-empty scriptSig", SCRIPT_VERIFY_WITNESS, false, WitnessMode::PKH)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey0)
                        .AsWit()
                        .Num(11)
                        .ScriptError(SCRIPT_ERR_WITNESS_MALLEATED));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1),
        "P2SH(P2WPKH) with superfluous push in scriptSig", SCRIPT_VERIFY_WITNESS, true, WitnessMode::PKH)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey1)
                        .AsWit()
                        .Num(11)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_MALLEATED_P2SH));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK ECDSA with witness", SCRIPT_VERIFY_WITNESS)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .Push("0")
                        .AsWit()
                        .ScriptError(SCRIPT_ERR_WITNESS_UNEXPECTED));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "P2PK SCHNORR with witness", SCRIPT_VERIFY_WITNESS)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .Push("0")
                        .AsWit()
                        .ScriptError(SCRIPT_ERR_WITNESS_UNEXPECTED));
    // Compressed keys should pass SCRIPT_VERIFY_WITNESS_PUBKEYTYPE
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
        "Basic P2WSH with compressed key", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::SH,
        0, 1)
                        .PushWitSig(keys.key0C)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C),
        "Basic P2WPKH with compressed key", SCRIPT_VERIFY_WITNESS  | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::PKH,
        0, 1)
                        .PushWitSig(keys.key0C)
                        .Push(keys.pubkey0C)
                        .AsWit());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
        "Basic P2SH(P2WSH) with compressed key", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::SH,
        0, 1)
                        .PushWitSig(keys.key0C)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C),
        "Basic P2SH(P2WPKH) with compressed key", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::PKH,
        0, 1)
                        .PushWitSig(keys.key0C)
                        .Push(keys.pubkey0C)
                        .AsWit()
                        .PushRedeem());

    // Testing uncompressed key in witness with SCRIPT_VERIFY_WITNESS_PUBKEYTYPE
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "Basic P2WSH", SCRIPT_VERIFY_WITNESS  | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::SH,
        0, 1)
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "Basic P2WPKH", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::PKH,
        0, 1)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey0)
                        .AsWit()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
        "Basic P2SH(P2WSH)", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::SH,
        0, 1)
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0),
        "Basic P2SH(P2WPKH)", SCRIPT_VERIFY_WITNESS| SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::PKH,
        0, 1)
                        .PushWitSig(keys.key0)
                        .Push(keys.pubkey0)
                        .AsWit()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));

    // P2WSH 1-of-2 multisig with compressed keys
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with compressed keys", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0C)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG with compressed keys", SCRIPT_VERIFY_WITNESS |  SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0C)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with compressed keys", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1C)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG with compressed keys", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1C)
                        .PushWitRedeem()
                        .PushRedeem());

    // P2WSH 1-of-2 multisig with first key uncompressed
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with first key uncompressed and signing with the first key", SCRIPT_VERIFY_WITNESS, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG first key uncompressed and signing with the first key", SCRIPT_VERIFY_WITNESS, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with first key uncompressed and signing with the first key", SCRIPT_VERIFY_WITNESS| SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG with first key uncompressed and signing with the first key", SCRIPT_VERIFY_WITNESS| SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0)
                        .PushWitRedeem()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with first key uncompressed and signing with the second key", SCRIPT_VERIFY_WITNESS, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1C)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG with first key uncompressed and signing with the second key", SCRIPT_VERIFY_WITNESS, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1C)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with first key uncompressed and signing with the second key", SCRIPT_VERIFY_WITNESS| SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1C)
                        .PushWitRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG with first key uncompressed and signing with the second key", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1C)
                        .PushWitRedeem()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));
    // P2WSH 1-of-2 multisig with second key uncompressed
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with second key uncompressed and signing with the first key", SCRIPT_VERIFY_WITNESS , false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0C)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG second key uncompressed and signing with the first key", SCRIPT_VERIFY_WITNESS , true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0C)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with second key uncompressed and signing with the first key should pass as the uncompressed key is not used", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0C)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG with second key uncompressed and signing with the first key should pass as the uncompressed key is not used", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key0C)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with second key uncompressed and signing with the second key", SCRIPT_VERIFY_WITNESS, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1)
                        .PushWitRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG with second key uncompressed and signing with the second key", SCRIPT_VERIFY_WITNESS, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1)
                        .PushWitRedeem()
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2WSH CHECKMULTISIG with second key uncompressed and signing with the second key", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, false, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1)
                        .PushWitRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1) << ToByteVector(keys.pubkey0C) << OP_2 << OP_CHECKMULTISIG,
        "P2SH(P2WSH) CHECKMULTISIG with second key uncompressed and signing with the second key", SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE, true, WitnessMode::SH,
        0, 1)
                        .Push(CScript())
                        .AsWit()
                        .PushWitSig(keys.key1)
                        .PushWitRedeem()
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_WITNESS_PUBKEYTYPE));

    // Test OP_CHECKDATASIG
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
        "Standard CHECKDATASIG ECDSA", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {})
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
        "Standard CHECKDATASIG SCHNORR", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::SCHNORR, {})
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG ECDSA with NULLFAIL flags", SCRIPT_VERIFY_NONE | SCRIPT_VERIFY_NULLFAIL)
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {})
                        .Num(1)
                        .ScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG SCHNORR with NULLFAIL flags", SCRIPT_VERIFY_NONE | SCRIPT_VERIFY_NULLFAIL)
                        .PushDataSig(keys.key1, SignatureScheme::SCHNORR, {})
                        .Num(1)
                        .ScriptError(SCRIPT_ERR_SIG_NULLFAIL));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG ECDSA without NULLFAIL flags", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {})
                        .Num(1));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG SCHNORR without NULLFAIL flags", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::SCHNORR, {})
                        .Num(1));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG empty signature", SCRIPT_VERIFY_NONE )
                        .Num(0)
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
        "CHECKDATASIG ECDSA with High S", SCRIPT_VERIFY_NONE )
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {}, 32, 33)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_SIG_HIGH_S));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
        "CHECKDATASIG ECDSA with too little R padding", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {}, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG,
        "CHECKDATASIG ECDSA with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::ECDSA, {})
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG,
        "CHECKDATASIG SCHNORR with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::SCHNORR, {})
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG ECDSA NOT with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::ECDSA,  {})
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG SCHNORR NOT with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::SCHNORR,  {})
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG ECDSA NOT with invalid signature and hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::ECDSA, {})
                        .DamagePush(10)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIG << OP_NOT,
        "CHECKDATASIG SCHNORR NOT with invalid signature and hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::SCHNORR, {})
                        .DamagePush(10)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    // Test OP_CHECKDATASIGVERIFY
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "Standard CHECKDATASIGVERIFY ECDSA", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {})
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "Standard CHECKDATASIGVERIFY SCHNORR", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::SCHNORR, {})
                        .Num(0));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIGVERIFY ECDSA without NULLFAIL flags", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {})
                        .Num(1)
                        .ScriptError(SCRIPT_ERR_CHECKDATASIGVERIFY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIGVERIFY SCHNORR without NULLFAIL flags", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::SCHNORR, {})
                        .Num(1)
                        .ScriptError(SCRIPT_ERR_CHECKDATASIGVERIFY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIGVERIFY empty signature", SCRIPT_VERIFY_NONE)
                        .Num(0)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_CHECKDATASIGVERIFY));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIG ECDSA with High S", SCRIPT_VERIFY_NONE )
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {}, 32, 33)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_SIG_HIGH_S));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIGVERIFY ECDSA with too little R padding", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {}, 33, 32)
                        .EditPush(1, "45022100", "440220")
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIGVERIFY ECDSA with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::ECDSA, {})
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIGVERIFY SCHNORR with hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::SCHNORR, {})
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIGVERIFY ECDSA with invalid signature and hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::ECDSA, {})
                        .DamagePush(10)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKDATASIGVERIFY << OP_TRUE,
        "CHECKDATASIGVERIFY SCHNORR with invalid signature and hybrid pubkey", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key0, SignatureScheme::SCHNORR, {})
                        .DamagePush(10)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_PUBKEYTYPE));
    std::set<std::string> tests_set;
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
        "CHECKDATASIG ECDSA with Hashtype byte", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::ECDSA, {})
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG,
        "CHECKDATASIG SCHNORR with Hashtype byte", SCRIPT_VERIFY_NONE)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR, {})
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "CHECKSIG ECDSA without Hashtype byte", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::ECDSA, {})
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    tests.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
        "CHECKSIG SCHNORR without Hashtype byte", SCRIPT_VERIFY_NONE)
                        .PushDataSig(keys.key1, SignatureScheme::SCHNORR, {})
                        .ScriptError(SCRIPT_ERR_SIG_DER));
    {
        UniValue json_tests = read_json(std::string(json_tests::script_tests));

        for (unsigned int idx = 0; idx < json_tests.size(); idx++) {
            const UniValue& tv = json_tests[idx];
            tests_set.insert(JSONPrettyPrint(tv.get_array()));
        }
    }

    std::string strGen;

    for (TestBuilder& test : tests) {
        test.Test();
        std::string str = JSONPrettyPrint(test.GetJSON());
#ifndef UPDATE_JSON_TESTS
        if (tests_set.count(str) == 0) {
            BOOST_CHECK_MESSAGE(false, "Missing auto script_valid test: " + test.GetComment() + "\n" + str);
        }
#endif
        strGen += str + ",\n";
    }

#ifdef UPDATE_JSON_TESTS
    FILE* file = fopen("script_tests.json.gen", "w");
    fputs(strGen.c_str(), file);
    fclose(file);
#endif
}

BOOST_AUTO_TEST_CASE(script_json_test)
{
    // Read tests from test/data/script_tests.json
    // Format is an array of arrays
    // Inner arrays are [ ["wit"..., nValue]?, "scriptSig", "scriptPubKey", "flags", "expected_scripterror" ]
    // ... where scriptSig and scriptPubKey are stringified
    // scripts.
    // If a witness is given, then the last value in the array should be the
    // amount (nValue) to use in the crediting tx
    UniValue tests = read_json(std::string(json_tests::script_tests));

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        CScriptWitness witness;
        CAmount nValue = 0;
        unsigned int pos = 0;
        if (test.size() > 0 && test[pos].isArray()) {
            unsigned int i = 0;
            for (i = 0; i < test[pos].size() - 1; i++) {
                witness.stack.push_back(ParseHex(test[pos][i].get_str()));
            }
            nValue = AmountFromValue(test[pos][i]);
            pos++;
        }
        if (test.size() < 4 + pos) // Allow size > 3; extra stuff ignored (useful for comments)
        {
            if (test.size() != 1) {
                BOOST_ERROR("Bad test: " << strTest);
            }
            continue;
        }
        std::string scriptSigString = test[pos++].get_str();
        CScript scriptSig = ParseScript(scriptSigString);
        std::string scriptPubKeyString = test[pos++].get_str();
        CScript scriptPubKey = ParseScript(scriptPubKeyString);
        unsigned int scriptflags = ParseScriptFlags(test[pos++].get_str());
        int scriptError = ParseScriptError(test[pos++].get_str());

        DoTest(scriptPubKey, scriptSig, witness, scriptflags, strTest, scriptError, nValue);
    }
}

BOOST_AUTO_TEST_CASE(script_PushData)
{
    // Check that PUSHDATA1, PUSHDATA2, and PUSHDATA4 create the same value on
    // the stack as the 1-75 opcodes do.
    static const unsigned char direct[] = {1, 0x5a};
    static const unsigned char pushdata1[] = {OP_PUSHDATA1, 1, 0x5a};
    static const unsigned char pushdata2[] = {OP_PUSHDATA2, 1, 0, 0x5a};
    static const unsigned char pushdata4[] = {OP_PUSHDATA4, 1, 0, 0, 0, 0x5a};

    //verify the same same set of tests using SCRIPT_VERIFY_NONE, stdFlags and mndFlags
    ScriptError err;
    std::vector<std::vector<unsigned char>> directStack;
    ColorIdentifier colorId;
    BOOST_CHECK(EvalScript(directStack, CScript(&direct[0], &direct[sizeof(direct)]), SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    std::vector<std::vector<unsigned char>> pushdata1Stack;
    BOOST_CHECK(!EvalScript(pushdata1Stack, CScript(&pushdata1[0], &pushdata1[sizeof(pushdata1)]), SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata1Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));

    std::vector<std::vector<unsigned char>> pushdata2Stack;
    BOOST_CHECK(!EvalScript(pushdata2Stack, CScript(&pushdata2[0], &pushdata2[sizeof(pushdata2)]), SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata2Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));

    std::vector<std::vector<unsigned char>> pushdata4Stack;
    BOOST_CHECK(!EvalScript(pushdata4Stack, CScript(&pushdata4[0], &pushdata4[sizeof(pushdata4)]), SCRIPT_VERIFY_NONE, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata4Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));

    //verify using stdFlags
    directStack.clear();
    pushdata1Stack.clear();
    pushdata2Stack.clear();
    pushdata4Stack.clear();

    BOOST_CHECK(EvalScript(directStack, CScript(&direct[0], &direct[sizeof(direct)]), stdFlags, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    BOOST_CHECK(!EvalScript(pushdata1Stack, CScript(&pushdata1[0], &pushdata1[sizeof(pushdata1)]), stdFlags, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata1Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));

    BOOST_CHECK(!EvalScript(pushdata2Stack, CScript(&pushdata2[0], &pushdata2[sizeof(pushdata2)]), stdFlags, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata2Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));

    BOOST_CHECK(!EvalScript(pushdata4Stack, CScript(&pushdata4[0], &pushdata4[sizeof(pushdata4)]), stdFlags, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata4Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));

    //verify using mndFlags
    directStack.clear();
    pushdata1Stack.clear();
    pushdata2Stack.clear();
    pushdata4Stack.clear();

    BOOST_CHECK(EvalScript(directStack, CScript(&direct[0], &direct[sizeof(direct)]), mndFlags, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    BOOST_CHECK(!EvalScript(pushdata1Stack, CScript(&pushdata1[0], &pushdata1[sizeof(pushdata1)]), mndFlags, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata1Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));

    BOOST_CHECK(!EvalScript(pushdata2Stack, CScript(&pushdata2[0], &pushdata2[sizeof(pushdata2)]), mndFlags, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata2Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));

    BOOST_CHECK(!EvalScript(pushdata4Stack, CScript(&pushdata4[0], &pushdata4[sizeof(pushdata4)]), mndFlags, BaseSignatureChecker(), SigVersion::BASE, &colorId, &err));
    //BOOST_CHECK(pushdata4Stack == directStack);
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_MINIMALDATA, ScriptErrorString(err));
}

static CScript
sign_multisig(const CScript& scriptPubKey, const std::vector<CKey>& keys, const CTransaction& transaction)
{
    uint256 hash = SignatureHash(scriptPubKey, transaction, 0, SIGHASH_ALL, 0, SigVersion::BASE);

    CScript result;
    //
    // NOTE: CHECKMULTISIG has an unfortunate bug; it requires
    // one extra item on the stack, before the signatures.
    // Putting OP_0 on the stack is the workaround;
    // fixing the bug would mean splitting the block chain (old
    // clients would not accept new CHECKMULTISIG transactions,
    // and vice-versa)
    //
    result << OP_0;
    for (const CKey& key : keys) {
        std::vector<unsigned char> vchSig;
        BOOST_CHECK(key.Sign_ECDSA(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        result << vchSig;
    }
    return result;
}
static CScript
sign_multisig(const CScript& scriptPubKey, const CKey& key, const CTransaction& transaction)
{
    std::vector<CKey> keys;
    keys.push_back(key);
    return sign_multisig(scriptPubKey, keys, transaction);
}

BOOST_AUTO_TEST_CASE(script_CHECKMULTISIG12)
{
    ScriptError err;
    ColorIdentifier colorId;
    CKey key1, key2, key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);

    CScript scriptPubKey12;
    scriptPubKey12 << OP_1 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    const CTransaction txFrom12{BuildCreditingTransaction(scriptPubKey12)};
    CMutableTransaction txTo12 = BuildSpendingTransaction(CScript(), CScriptWitness(), txFrom12);

    CScript goodsig1 = sign_multisig(scriptPubKey12, key1, txTo12);
    BOOST_CHECK(VerifyScript(goodsig1, scriptPubKey12, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    BOOST_CHECK(VerifyScript(goodsig1, scriptPubKey12, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    BOOST_CHECK(VerifyScript(goodsig1, scriptPubKey12, nullptr, mndFlags, 
    MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    txTo12.vout[0].nValue = 2;
    BOOST_CHECK(!VerifyScript(goodsig1, scriptPubKey12, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));
    BOOST_CHECK(!VerifyScript(goodsig1, scriptPubKey12, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));
    BOOST_CHECK(!VerifyScript(goodsig1, scriptPubKey12, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    CScript goodsig2 = sign_multisig(scriptPubKey12, key2, txTo12);
    BOOST_CHECK(VerifyScript(goodsig2, scriptPubKey12, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    BOOST_CHECK(VerifyScript(goodsig2, scriptPubKey12, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    BOOST_CHECK(VerifyScript(goodsig2, scriptPubKey12, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    CScript badsig1 = sign_multisig(scriptPubKey12, key3, txTo12);
    BOOST_CHECK(!VerifyScript(badsig1, scriptPubKey12, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));
    BOOST_CHECK(!VerifyScript(badsig1, scriptPubKey12, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));
    BOOST_CHECK(!VerifyScript(badsig1, scriptPubKey12, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));
}

BOOST_AUTO_TEST_CASE(script_CHECKMULTISIG23)
{
    ScriptError err;
    ColorIdentifier colorId;
    CKey key1, key2, key3, key4;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);
    key4.MakeNewKey(false);

    CScript scriptPubKey23;
    scriptPubKey23 << OP_2 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey()) << ToByteVector(key3.GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    const CTransaction txFrom23{BuildCreditingTransaction(scriptPubKey23)};
    CMutableTransaction txTo23 = BuildSpendingTransaction(CScript(), CScriptWitness(), txFrom23);

    std::vector<CKey> keys;
    keys.push_back(key1);
    keys.push_back(key2);
    CScript goodsig1 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(VerifyScript(goodsig1, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    BOOST_CHECK(VerifyScript(goodsig1, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    BOOST_CHECK(VerifyScript(goodsig1, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key1);
    keys.push_back(key3);
    CScript goodsig2 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(VerifyScript(goodsig2, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    BOOST_CHECK(VerifyScript(goodsig2, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    BOOST_CHECK(VerifyScript(goodsig2, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key2);
    keys.push_back(key3);
    CScript goodsig3 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(VerifyScript(goodsig3, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    BOOST_CHECK(VerifyScript(goodsig3, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    BOOST_CHECK(VerifyScript(goodsig3, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key2);
    keys.push_back(key2); // Can't re-use sig
    CScript badsig1 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(!VerifyScript(badsig1, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig1, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig1, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key2);
    keys.push_back(key1); // sigs must be in correct order
    CScript badsig2 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(!VerifyScript(badsig2, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig2, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig2, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key3);
    keys.push_back(key2); // sigs must be in correct order
    CScript badsig3 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(!VerifyScript(badsig3, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig3, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig3, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key4);
    keys.push_back(key2); // sigs must match pubkeys
    CScript badsig4 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(!VerifyScript(badsig4, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig4, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig4, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    keys.clear();
    keys.push_back(key1);
    keys.push_back(key4); // sigs must match pubkeys
    CScript badsig5 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(!VerifyScript(badsig5, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_EVAL_FALSE, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig5, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    BOOST_CHECK(!VerifyScript(badsig5, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_SIG_NULLFAIL, ScriptErrorString(err));

    keys.clear(); // Must have signatures
    CScript badsig6 = sign_multisig(scriptPubKey23, keys, txTo23);
    BOOST_CHECK(!VerifyScript(badsig6, scriptPubKey23, nullptr, SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_INVALID_STACK_OPERATION, ScriptErrorString(err));
    BOOST_CHECK(!VerifyScript(badsig6, scriptPubKey23, nullptr, stdFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_INVALID_STACK_OPERATION, ScriptErrorString(err));
    BOOST_CHECK(!VerifyScript(badsig6, scriptPubKey23, nullptr, mndFlags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), colorId, &err));
    BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_INVALID_STACK_OPERATION, ScriptErrorString(err));
}

/* Wrapper around ProduceSignature to combine two scriptsigs */
SignatureData CombineSignatures(const CTxOut& txout, const CMutableTransaction& tx, const SignatureData& scriptSig1, const SignatureData& scriptSig2)
{
    SignatureData data;
    data.MergeSignatureData(scriptSig1);
    data.MergeSignatureData(scriptSig2);
    ProduceSignature(DUMMY_SIGNING_PROVIDER, MutableTransactionSignatureCreator(&tx, 0, txout.nValue), txout.scriptPubKey, data);
    return data;
}

BOOST_AUTO_TEST_CASE(script_combineSigs)
{
    // Test the ProduceSignature's ability to combine signatures function
    CBasicKeyStore keystore;
    std::vector<CKey> keys;
    std::vector<CPubKey> pubkeys;
    for (int i = 0; i < 3; i++) {
        CKey key;
        key.MakeNewKey(i % 2 == 1);
        keys.push_back(key);
        pubkeys.push_back(key.GetPubKey());
        keystore.AddKey(key);
    }

    CMutableTransaction txFrom = BuildCreditingTransaction(GetScriptForDestination(keys[0].GetPubKey().GetID()));
    CMutableTransaction txTo = BuildSpendingTransaction(CScript(), CScriptWitness(), txFrom);
    CScript& scriptPubKey = txFrom.vout[0].scriptPubKey;
    SignatureData scriptSig;

    SignatureData empty;
    SignatureData combined = CombineSignatures(txFrom.vout[0], txTo, empty, empty);
    BOOST_CHECK(combined.scriptSig.empty());

    // Single signature case:
    SignSignature(keystore, txFrom, txTo, 0, SIGHASH_ALL); // changes scriptSig
    scriptSig = DataFromTransaction(txTo, 0, txFrom.vout[0]);
    combined = CombineSignatures(txFrom.vout[0], txTo, scriptSig, empty);
    BOOST_CHECK(combined.scriptSig == scriptSig.scriptSig);
    combined = CombineSignatures(txFrom.vout[0], txTo, empty, scriptSig);
    BOOST_CHECK(combined.scriptSig == scriptSig.scriptSig);
    SignatureData scriptSigCopy = scriptSig;
    // Signing again will give a different, valid signature:
    SignSignature(keystore, txFrom, txTo, 0, SIGHASH_ALL);
    scriptSig = DataFromTransaction(txTo, 0, txFrom.vout[0]);
    combined = CombineSignatures(txFrom.vout[0], txTo, scriptSigCopy, scriptSig);
    bool copy = combined.scriptSig == scriptSigCopy.scriptSig;
    bool sig = combined.scriptSig == scriptSig.scriptSig;
    bool res = copy || sig;
    BOOST_CHECK(res);

    // P2SH, single-signature case:
    CScript pkSingle;
    pkSingle << ToByteVector(keys[0].GetPubKey()) << OP_CHECKSIG;
    keystore.AddCScript(pkSingle);
    scriptPubKey = GetScriptForDestination(CScriptID(pkSingle));
    SignSignature(keystore, txFrom, txTo, 0, SIGHASH_ALL);
    scriptSig = DataFromTransaction(txTo, 0, txFrom.vout[0]);
    combined = CombineSignatures(txFrom.vout[0], txTo, scriptSig, empty);
    BOOST_CHECK(combined.scriptSig == scriptSig.scriptSig);
    combined = CombineSignatures(txFrom.vout[0], txTo, empty, scriptSig);
    BOOST_CHECK(combined.scriptSig == scriptSig.scriptSig);
    scriptSigCopy = scriptSig;
    SignSignature(keystore, txFrom, txTo, 0, SIGHASH_ALL);
    scriptSig = DataFromTransaction(txTo, 0, txFrom.vout[0]);
    combined = CombineSignatures(txFrom.vout[0], txTo, scriptSigCopy, scriptSig);
    copy = combined.scriptSig == scriptSigCopy.scriptSig;
    sig = combined.scriptSig == scriptSig.scriptSig;
    res = copy || sig;
    BOOST_TEST(res);

    // Hardest case:  Multisig 2-of-3
    scriptPubKey = GetScriptForMultisig(2, pubkeys);
    keystore.AddCScript(scriptPubKey);
    SignSignature(keystore, txFrom, txTo, 0, SIGHASH_ALL);
    scriptSig = DataFromTransaction(txTo, 0, txFrom.vout[0]);
    combined = CombineSignatures(txFrom.vout[0], txTo, scriptSig, empty);
    BOOST_CHECK(combined.scriptSig == scriptSig.scriptSig);
    combined = CombineSignatures(txFrom.vout[0], txTo, empty, scriptSig);
    BOOST_CHECK(combined.scriptSig == scriptSig.scriptSig);

    // A couple of partially-signed versions:
    std::vector<unsigned char> sig1;
    uint256 hash1 = SignatureHash(scriptPubKey, txTo, 0, SIGHASH_ALL, 0, SigVersion::BASE);
    BOOST_CHECK(keys[0].Sign_ECDSA(hash1, sig1));
    sig1.push_back(SIGHASH_ALL);
    std::vector<unsigned char> sig2;
    uint256 hash2 = SignatureHash(scriptPubKey, txTo, 0, SIGHASH_NONE, 0, SigVersion::BASE);
    BOOST_CHECK(keys[1].Sign_ECDSA(hash2, sig2));
    sig2.push_back(SIGHASH_NONE);
    std::vector<unsigned char> sig3;
    uint256 hash3 = SignatureHash(scriptPubKey, txTo, 0, SIGHASH_SINGLE, 0, SigVersion::BASE);
    BOOST_CHECK(keys[2].Sign_ECDSA(hash3, sig3));
    sig3.push_back(SIGHASH_SINGLE);

    // Not fussy about order (or even existence) of placeholders or signatures:
    CScript partial1a = CScript() << OP_0 << sig1 << OP_0;
    CScript partial1b = CScript() << OP_0 << OP_0 << sig1;
    CScript partial2a = CScript() << OP_0 << sig2;
    CScript partial2b = CScript() << sig2 << OP_0;
    CScript partial3a = CScript() << sig3;
    CScript partial3b = CScript() << OP_0 << OP_0 << sig3;
    CScript partial3c = CScript() << OP_0 << sig3 << OP_0;
    CScript complete12 = CScript() << OP_0 << sig1 << sig2;
    CScript complete13 = CScript() << OP_0 << sig1 << sig3;
    CScript complete23 = CScript() << OP_0 << sig2 << sig3;
    SignatureData partial1_sigs;
    partial1_sigs.signatures.emplace(keys[0].GetPubKey().GetID(), SigPair(keys[0].GetPubKey(), sig1));
    SignatureData partial2_sigs;
    partial2_sigs.signatures.emplace(keys[1].GetPubKey().GetID(), SigPair(keys[1].GetPubKey(), sig2));
    SignatureData partial3_sigs;
    partial3_sigs.signatures.emplace(keys[2].GetPubKey().GetID(), SigPair(keys[2].GetPubKey(), sig3));

    combined = CombineSignatures(txFrom.vout[0], txTo, partial1_sigs, partial1_sigs);
    BOOST_CHECK(combined.scriptSig == partial1a);
    combined = CombineSignatures(txFrom.vout[0], txTo, partial1_sigs, partial2_sigs);
    BOOST_CHECK(combined.scriptSig == complete12);
    combined = CombineSignatures(txFrom.vout[0], txTo, partial2_sigs, partial1_sigs);
    BOOST_CHECK(combined.scriptSig == complete12);
    combined = CombineSignatures(txFrom.vout[0], txTo, partial1_sigs, partial2_sigs);
    BOOST_CHECK(combined.scriptSig == complete12);
    combined = CombineSignatures(txFrom.vout[0], txTo, partial3_sigs, partial1_sigs);
    BOOST_CHECK(combined.scriptSig == complete13);
    combined = CombineSignatures(txFrom.vout[0], txTo, partial2_sigs, partial3_sigs);
    BOOST_CHECK(combined.scriptSig == complete23);
    combined = CombineSignatures(txFrom.vout[0], txTo, partial3_sigs, partial2_sigs);
    BOOST_CHECK(combined.scriptSig == complete23);
    combined = CombineSignatures(txFrom.vout[0], txTo, partial3_sigs, partial3_sigs);
    BOOST_CHECK(combined.scriptSig == partial3c);
}

BOOST_AUTO_TEST_CASE(script_standard_push)
{
    ScriptError err;
    ColorIdentifier colorId;
    for (int i = 0; i < 67000; i++) {
        CScript script;
        script << i;
        BOOST_CHECK_MESSAGE(script.IsPushOnly(), "Number " << i << " is not pure push.");
        BOOST_CHECK_MESSAGE(VerifyScript(script, CScript() << i << OP_EQUAL, nullptr, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), colorId, &err), "Number " << i << " push is not minimal data.");
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
        BOOST_CHECK_MESSAGE(VerifyScript(script, CScript() << i << OP_EQUAL, nullptr, stdFlags, BaseSignatureChecker(), colorId, &err), "Number " << i << " push is not minimal data.");
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
        BOOST_CHECK_MESSAGE(VerifyScript(script, CScript() << i << OP_EQUAL, nullptr, mndFlags, BaseSignatureChecker(), colorId, &err), "Number " << i << " push is not minimal data.");
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    }

    for (unsigned int i = 0; i <= MAX_SCRIPT_ELEMENT_SIZE; i++) {
        std::vector<unsigned char> data(i, '\111');
        CScript script;
        script << data;
        BOOST_CHECK_MESSAGE(script.IsPushOnly(), "Length " << i << " is not pure push.");
        BOOST_CHECK_MESSAGE(VerifyScript(script, CScript() << data << OP_EQUAL, nullptr, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), colorId, &err), "Length " << i << " push is not minimal data.");
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
        BOOST_CHECK_MESSAGE(VerifyScript(script, CScript() << data << OP_EQUAL, nullptr, stdFlags, BaseSignatureChecker(), colorId, &err), "Length " << i << " push is not minimal data.");
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
        BOOST_CHECK_MESSAGE(VerifyScript(script, CScript() << data << OP_EQUAL, nullptr, mndFlags, BaseSignatureChecker(), colorId, &err), "Length " << i << " push is not minimal data.");
        BOOST_CHECK_MESSAGE(err == SCRIPT_ERR_OK, ScriptErrorString(err));
    }
}

BOOST_AUTO_TEST_CASE(script_IsPushOnly_on_invalid_scripts)
{
    // IsPushOnly returns false when given a script containing only pushes that
    // are invalid due to truncation. IsPushOnly() is consensus critical
    // because P2SH evaluation uses it, although this specific behavior should
    // not be consensus critical as the P2SH evaluation would fail first due to
    // the invalid push. Still, it doesn't hurt to test it explicitly.
    static const unsigned char direct[] = {1};
    BOOST_CHECK(!CScript(direct, direct + sizeof(direct)).IsPushOnly());
}

BOOST_AUTO_TEST_CASE(script_GetScriptAsm)
{
    BOOST_CHECK_EQUAL("OP_CHECKLOCKTIMEVERIFY", ScriptToAsmStr(CScript() << OP_NOP2, true));
    BOOST_CHECK_EQUAL("OP_CHECKLOCKTIMEVERIFY", ScriptToAsmStr(CScript() << OP_CHECKLOCKTIMEVERIFY, true));
    BOOST_CHECK_EQUAL("OP_CHECKLOCKTIMEVERIFY", ScriptToAsmStr(CScript() << OP_NOP2));
    BOOST_CHECK_EQUAL("OP_CHECKLOCKTIMEVERIFY", ScriptToAsmStr(CScript() << OP_CHECKLOCKTIMEVERIFY));

    std::string derSig("3044022065d7fef0523494dd704e8b4f55860e815eb5bf80e12076e816d9fa76175ac40f0220700bfe5809f57653cfdf3b4b467225f3ed83b36c3384533c26ce657b35063ae2");
    std::string pubKey("038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508");
    std::vector<unsigned char> vchPubKey = ToByteVector(ParseHex(pubKey));

    BOOST_CHECK_EQUAL(derSig + "00 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "00")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "80 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "80")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[ALL] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "01")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[NONE] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "02")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[SINGLE] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[ALL|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "81")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[NONE|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "82")) << vchPubKey, true));
    BOOST_CHECK_EQUAL(derSig + "[SINGLE|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey, true));

    BOOST_CHECK_EQUAL(derSig + "00 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "00")) << vchPubKey));
    BOOST_CHECK_EQUAL(derSig + "80 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "80")) << vchPubKey));
    BOOST_CHECK_EQUAL(derSig + "01 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "01")) << vchPubKey));
    BOOST_CHECK_EQUAL(derSig + "02 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "02")) << vchPubKey));
    BOOST_CHECK_EQUAL(derSig + "03 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey));
    BOOST_CHECK_EQUAL(derSig + "81 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "81")) << vchPubKey));
    BOOST_CHECK_EQUAL(derSig + "82 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "82")) << vchPubKey));
    BOOST_CHECK_EQUAL(derSig + "83 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey));
}

static CScript
ScriptFromHex(const char* hex)
{
    std::vector<unsigned char> data = ParseHex(hex);
    return CScript(data.begin(), data.end());
}


BOOST_AUTO_TEST_CASE(script_FindAndDelete)
{
    // Exercise the FindAndDelete functionality
    CScript s;
    CScript d;
    CScript expect;

    s = CScript() << OP_1 << OP_2;
    d = CScript(); // delete nothing should be a no-op
    expect = s;
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 0);
    BOOST_CHECK(s == expect);

    s = CScript() << OP_1 << OP_2 << OP_3;
    d = CScript() << OP_2;
    expect = CScript() << OP_1 << OP_3;
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 1);
    BOOST_CHECK(s == expect);

    s = CScript() << OP_3 << OP_1 << OP_3 << OP_3 << OP_4 << OP_3;
    d = CScript() << OP_3;
    expect = CScript() << OP_1 << OP_4;
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 4);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0302ff03"); // PUSH 0x02ff03 onto stack
    d = ScriptFromHex("0302ff03");
    expect = CScript();
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 1);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0302ff030302ff03"); // PUSH 0x2ff03 PUSH 0x2ff03
    d = ScriptFromHex("0302ff03");
    expect = CScript();
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 2);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0302ff030302ff03");
    d = ScriptFromHex("02");
    expect = s; // FindAndDelete matches entire opcodes
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 0);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0302ff030302ff03");
    d = ScriptFromHex("ff");
    expect = s;
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 0);
    BOOST_CHECK(s == expect);

    // This is an odd edge case: strip of the push-three-bytes
    // prefix, leaving 02ff03 which is push-two-bytes:
    s = ScriptFromHex("0302ff030302ff03");
    d = ScriptFromHex("03");
    expect = CScript() << ParseHex("ff03") << ParseHex("ff03");
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 2);
    BOOST_CHECK(s == expect);

    // Byte sequence that spans multiple opcodes:
    s = ScriptFromHex("02feed5169"); // PUSH(0xfeed) OP_1 OP_VERIFY
    d = ScriptFromHex("feed51");
    expect = s;
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 0); // doesn't match 'inside' opcodes
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("02feed5169"); // PUSH(0xfeed) OP_1 OP_VERIFY
    d = ScriptFromHex("02feed51");
    expect = ScriptFromHex("69");
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 1);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("516902feed5169");
    d = ScriptFromHex("feed51");
    expect = s;
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 0);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("516902feed5169");
    d = ScriptFromHex("02feed51");
    expect = ScriptFromHex("516969");
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 1);
    BOOST_CHECK(s == expect);

    s = CScript() << OP_0 << OP_0 << OP_1 << OP_1;
    d = CScript() << OP_0 << OP_1;
    expect = CScript() << OP_0 << OP_1; // FindAndDelete is single-pass
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 1);
    BOOST_CHECK(s == expect);

    s = CScript() << OP_0 << OP_0 << OP_1 << OP_0 << OP_1 << OP_1;
    d = CScript() << OP_0 << OP_1;
    expect = CScript() << OP_0 << OP_1; // FindAndDelete is single-pass
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 2);
    BOOST_CHECK(s == expect);

    // Another weird edge case:
    // End with invalid push (not enough data)...
    s = ScriptFromHex("0003feed");
    d = ScriptFromHex("03feed"); // ... can remove the invalid push
    expect = ScriptFromHex("00");
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 1);
    BOOST_CHECK(s == expect);

    s = ScriptFromHex("0003feed");
    d = ScriptFromHex("00");
    expect = ScriptFromHex("03feed");
    BOOST_CHECK_EQUAL(FindAndDelete(s, d), 1);
    BOOST_CHECK(s == expect);
}

BOOST_AUTO_TEST_CASE(script_HasValidOps)
{
    // Exercise the HasValidOps functionality
    CScript script;
    script = ScriptFromHex("76a9141234567890abcdefa1a2a3a4a5a6a7a8a9a0aaab88ac"); // Normal script
    BOOST_CHECK(script.HasValidOps());
    script = ScriptFromHex("76a914ff34567890abcdefa1a2a3a4a5a6a7a8a9a0aaab88ac");
    BOOST_CHECK(script.HasValidOps());
    script = ScriptFromHex("ff88ac"); // Script with OP_INVALIDOPCODE explicit
    BOOST_CHECK(!script.HasValidOps());
    script = ScriptFromHex("88acc0"); // Script with undefined opcode
    BOOST_CHECK(!script.HasValidOps());
}

BOOST_AUTO_TEST_CASE(script_can_append_self)
{
    CScript s, d;

    s = ScriptFromHex("00");
    s += s;
    d = ScriptFromHex("0000");
    BOOST_CHECK(s == d);

    // check doubling a script that's large enough to require reallocation
    static const char hex[] = "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f";
    s = CScript() << ParseHex(hex) << OP_CHECKSIG;
    d = CScript() << ParseHex(hex) << OP_CHECKSIG << ParseHex(hex) << OP_CHECKSIG;
    s += s;
    BOOST_CHECK(s == d);
}

// -----------------------------------------------------------------------
// Colored coin script execution tests
//
// These tests exercise VerifyScript directly for CP2PKH and CP2SH scripts
// across all three token types (REISSUABLE, NON_REISSUABLE, NFT) and for
// the three token operations (issuance output, transfer, burn).
//
// At the script-execution level issuance/transfer/burn all spend a CP2PKH
// output — the distinction is at the transaction level.  What we verify
// here is that:
//   - valid signatures are accepted for every token type and sig scheme
//   - bad signatures and bad pubkeys are rejected with the right error
//   - OP_COLOR error conditions (invalid colorId, multiple OP_COLOR,
//     OP_COLOR inside a branch) are all caught
// -----------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(colored_coin_script_execution)
{
    const KeyData keys;

    // REISSUABLE colorId — derived from a P2PK script
    ColorIdentifier reissuableCid(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG);

    // NON_REISSUABLE and NFT colorIds — derived from a fixed COutPoint
    COutPoint outpoint(uint256S("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"), 0);
    ColorIdentifier nonReissuableCid(outpoint, TokenTypes::NON_REISSUABLE);
    ColorIdentifier nftCid(outpoint, TokenTypes::NFT);

    // Standard CP2PKH scriptPubKey for a given colorId
    auto cp2pkh = [&](const ColorIdentifier& cid) {
        return CScript() << cid.toVector() << OP_COLOR
                         << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID())
                         << OP_EQUALVERIFY << OP_CHECKSIG;
    };
    // Inner P2PKH script used as the CP2SH redeem script
    CScript cp2sh_inner = CScript() << OP_DUP << OP_HASH160
                                    << ToByteVector(keys.pubkey1C.GetID())
                                    << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<TestBuilder> tests;

    // -- REISSUABLE --
    // issuance output: script of the newly created colored UTXO
    tests.push_back(TestBuilder(cp2pkh(reissuableCid),
        "CP2PKH REISSUABLE issuance output ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(reissuableCid),
        "CP2PKH REISSUABLE issuance output SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    // transfer: spending the colored UTXO to move it to a new address
    tests.push_back(TestBuilder(cp2pkh(reissuableCid),
        "CP2PKH REISSUABLE transfer ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(reissuableCid),
        "CP2PKH REISSUABLE transfer SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    // burn: spending the colored UTXO with no colored output in the spending tx
    tests.push_back(TestBuilder(cp2pkh(reissuableCid),
        "CP2PKH REISSUABLE burn ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(reissuableCid),
        "CP2PKH REISSUABLE burn SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(reissuableCid),
        "CP2PKH REISSUABLE bad sig", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .DamagePush(10)
                        .Push(keys.pubkey1C)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(cp2pkh(reissuableCid),
        "CP2PKH REISSUABLE bad pubkey", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .DamagePush(5)
                        .ScriptError(SCRIPT_ERR_EQUALVERIFY));

    // -- NON_REISSUABLE --
    tests.push_back(TestBuilder(cp2pkh(nonReissuableCid),
        "CP2PKH NON_REISSUABLE issuance output ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nonReissuableCid),
        "CP2PKH NON_REISSUABLE issuance output SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nonReissuableCid),
        "CP2PKH NON_REISSUABLE transfer ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nonReissuableCid),
        "CP2PKH NON_REISSUABLE transfer SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nonReissuableCid),
        "CP2PKH NON_REISSUABLE burn ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nonReissuableCid),
        "CP2PKH NON_REISSUABLE burn SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nonReissuableCid),
        "CP2PKH NON_REISSUABLE bad sig", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .DamagePush(10)
                        .Push(keys.pubkey1C)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(cp2pkh(nonReissuableCid),
        "CP2PKH NON_REISSUABLE bad pubkey", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .DamagePush(5)
                        .ScriptError(SCRIPT_ERR_EQUALVERIFY));

    // -- NFT --
    tests.push_back(TestBuilder(cp2pkh(nftCid),
        "CP2PKH NFT issuance output ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nftCid),
        "CP2PKH NFT issuance output SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nftCid),
        "CP2PKH NFT transfer ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nftCid),
        "CP2PKH NFT transfer SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nftCid),
        "CP2PKH NFT burn ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nftCid),
        "CP2PKH NFT burn SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(cp2pkh(nftCid),
        "CP2PKH NFT bad sig", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .DamagePush(10)
                        .Push(keys.pubkey1C)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    tests.push_back(TestBuilder(cp2pkh(nftCid),
        "CP2PKH NFT bad pubkey", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .DamagePush(5)
                        .ScriptError(SCRIPT_ERR_EQUALVERIFY));

    // -- CP2SH(P2PKH) for each token type --
    tests.push_back(TestBuilder(CScript() << reissuableCid.toVector() << OP_COLOR
                                          << OP_HASH160 << ToByteVector(CScriptID(cp2sh_inner)) << OP_EQUAL,
        "CP2SH(P2PKH) REISSUABLE ECDSA", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << nonReissuableCid.toVector() << OP_COLOR
                                          << OP_HASH160 << ToByteVector(CScriptID(cp2sh_inner)) << OP_EQUAL,
        "CP2SH(P2PKH) NON_REISSUABLE ECDSA", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << nftCid.toVector() << OP_COLOR
                                          << OP_HASH160 << ToByteVector(CScriptID(cp2sh_inner)) << OP_EQUAL,
        "CP2SH(P2PKH) NFT ECDSA", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .PushRedeem());

    // -- OP_COLOR error conditions --

    // Invalid colorId: type byte 0x00 (NONE) is not a valid token type in a script
    {
        std::vector<unsigned char> invalidCid(33, 0x00);
        tests.push_back(TestBuilder(CScript() << invalidCid << OP_COLOR
                                              << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID())
                                              << OP_EQUALVERIFY << OP_CHECKSIG,
            "CP2PKH invalid colorId (type NONE)", 0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA)
                            .Push(keys.pubkey1C)
                            .ScriptError(SCRIPT_ERR_OP_COLORID_INVALID));
    }

    // Multiple OP_COLOR: second OP_COLOR fires SCRIPT_ERR_OP_COLORMULTIPLE
    tests.push_back(TestBuilder(CScript() << reissuableCid.toVector() << OP_COLOR
                                          << nftCid.toVector() << OP_COLOR
                                          << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG,
        "CP2PKH multiple OP_COLOR", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .ScriptError(SCRIPT_ERR_OP_COLORMULTIPLE));

    // OP_COLOR inside an IF branch: OP_1 guarantees the branch is always entered
    tests.push_back(TestBuilder(CScript() << OP_1 << OP_IF
                                          << reissuableCid.toVector() << OP_COLOR
                                          << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID())
                                          << OP_EQUALVERIFY << OP_CHECKSIG
                                          << OP_ENDIF,
        "CP2PKH OP_COLOR in IF branch", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .ScriptError(SCRIPT_ERR_OP_COLORINBRANCH));

    for (TestBuilder& test : tests) {
        test.Test();
    }
}

BOOST_AUTO_TEST_CASE(colored_coin_standard_scripts)
{
    const KeyData keys;

    // REISSUABLE colorId derived from pubkey0C P2PK script
    ColorIdentifier cid(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG);

    // CP2PKH scriptPubKey: <colorId> OP_COLOR OP_DUP OP_HASH160 <hash> OP_EQUALVERIFY OP_CHECKSIG
    CScript cp2pkh;
    cp2pkh << cid.toVector() << OP_COLOR
           << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID())
           << OP_EQUALVERIFY << OP_CHECKSIG;

    // CP2SH inner script: P2PKH for pubkey1C
    CScript innerP2PKH;
    innerP2PKH << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID())
               << OP_EQUALVERIFY << OP_CHECKSIG;

    // CP2SH scriptPubKey: <colorId> OP_COLOR OP_HASH160 <scriptHash> OP_EQUAL
    CScript cp2sh;
    cp2sh << cid.toVector() << OP_COLOR
          << OP_HASH160 << ToByteVector(CScriptID(innerP2PKH)) << OP_EQUAL;

    std::vector<TestBuilder> tests;

    // CP2PKH — valid ECDSA spend
    tests.push_back(TestBuilder(cp2pkh, "CP2PKH ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));

    // CP2PKH — valid SCHNORR spend
    tests.push_back(TestBuilder(cp2pkh, "CP2PKH SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));

    // P2SH(CP2PKH) — OP_COLOR inside a P2SH redeem script is forbidden
    tests.push_back(TestBuilder(cp2pkh,
        "P2SH(CP2PKH) ECDSA is invalid", SCRIPT_VERIFY_NONE,
        /*P2SH=*/true, WitnessMode::NONE, 0, 0, /*ignoreColor=*/true)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .Push(keys.pubkey0)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_OP_COLOR_UNEXPECTED));

    tests.push_back(TestBuilder(cp2pkh,
        "P2SH(CP2PKH) SCHNORR is invalid", SCRIPT_VERIFY_NONE,
        /*P2SH=*/true, WitnessMode::NONE, 0, 0, /*ignoreColor=*/true)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey0)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_OP_COLOR_UNEXPECTED));

    // CP2SH(P2PKH) — colored P2SH where the outer script has OP_COLOR and the
    // redeem script is a plain P2PKH; valid ECDSA and SCHNORR spends
    tests.push_back(TestBuilder(cp2sh, "CP2SH(P2PKH) ECDSA", SCRIPT_VERIFY_NONE, /*P2SH=*/true)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .PushRedeem());

    tests.push_back(TestBuilder(cp2sh, "CP2SH(P2PKH) SCHNORR", SCRIPT_VERIFY_NONE, /*P2SH=*/true)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C)
                        .PushRedeem());

    for (TestBuilder& test : tests) {
        test.Test();
    }
}

BOOST_AUTO_TEST_CASE(colored_coin_arithmetic_scripts)
{
    const KeyData keys;

    // Use REISSUABLE colorId for all arithmetic script tests
    ColorIdentifier cid(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG);

    // Helper: wrap an arithmetic scriptPubKey fragment with <colorId> OP_COLOR
    auto colored = [&](CScript inner) {
        CScript s;
        s << cid.toVector() << OP_COLOR;
        s += inner;
        return s;
    };

    std::vector<TestBuilder> tests;

    // OP_ADD: scriptSig pushes 6; scriptPubKey has OP_9 OP_ADD 15 OP_NUMEQUAL
    // Stack after scriptSig: [6]
    // After OP_9: [6, 9]
    // After OP_ADD: [15]
    // After 15: [15, 15]
    // After OP_NUMEQUAL: [1] — pass
    tests.push_back(TestBuilder(colored(CScript() << OP_9 << OP_ADD << 15 << OP_NUMEQUAL),
        "Colored OP_ADD valid (6+9=15)", 0)
                        .Num(6));
    tests.push_back(TestBuilder(colored(CScript() << OP_9 << OP_ADD << 15 << OP_NUMEQUAL),
        "Colored OP_ADD invalid (7+9=16 != 15)", 0)
                        .Num(7)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    // OP_SUB: scriptSig pushes 10 then 7; scriptPubKey has OP_SUB 3 OP_NUMEQUAL
    // Stack after scriptSig: [10, 7]
    // After OP_SUB: [3]  (10 - 7)
    // After 3: [3, 3]
    // After OP_NUMEQUAL: [1] — pass
    tests.push_back(TestBuilder(colored(CScript() << OP_SUB << 3 << OP_NUMEQUAL),
        "Colored OP_SUB valid (10-7=3)", 0)
                        .Num(10).Num(7));
    tests.push_back(TestBuilder(colored(CScript() << OP_SUB << 3 << OP_NUMEQUAL),
        "Colored OP_SUB invalid (10-8=2 != 3)", 0)
                        .Num(10).Num(8)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    // OP_WITHIN: scriptSig pushes 7; scriptPubKey has 5 10 OP_WITHIN
    // Stack after scriptSig: [7]
    // After 5: [7, 5]
    // After 10: [7, 5, 10]
    // After OP_WITHIN: [1]  (5 <= 7 < 10) — pass
    tests.push_back(TestBuilder(colored(CScript() << 5 << 10 << OP_WITHIN),
        "Colored OP_WITHIN valid (5<=7<10)", 0)
                        .Num(7));
    tests.push_back(TestBuilder(colored(CScript() << 5 << 10 << OP_WITHIN),
        "Colored OP_WITHIN invalid (4 < 5)", 0)
                        .Num(4)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    // OP_MIN: scriptSig pushes 3 and 7; scriptPubKey has OP_MIN 3 OP_NUMEQUAL
    // Stack after scriptSig: [3, 7]
    // After OP_MIN: [3]
    // After 3: [3, 3]
    // After OP_NUMEQUAL: [1] — pass
    tests.push_back(TestBuilder(colored(CScript() << OP_MIN << 3 << OP_NUMEQUAL),
        "Colored OP_MIN valid (min(3,7)=3)", 0)
                        .Num(3).Num(7));
    tests.push_back(TestBuilder(colored(CScript() << OP_MIN << 3 << OP_NUMEQUAL),
        "Colored OP_MIN invalid (min(5,7)=5 != 3)", 0)
                        .Num(5).Num(7)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    // OP_MAX: scriptSig pushes 3 and 7; scriptPubKey has OP_MAX 7 OP_NUMEQUAL
    // Stack after scriptSig: [3, 7]
    // After OP_MAX: [7]
    // After 7: [7, 7]
    // After OP_NUMEQUAL: [1] — pass
    tests.push_back(TestBuilder(colored(CScript() << OP_MAX << 7 << OP_NUMEQUAL),
        "Colored OP_MAX valid (max(3,7)=7)", 0)
                        .Num(3).Num(7));
    tests.push_back(TestBuilder(colored(CScript() << OP_MAX << 7 << OP_NUMEQUAL),
        "Colored OP_MAX invalid (max(3,6)=6 != 7)", 0)
                        .Num(3).Num(6)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    // OP_ABS: scriptSig pushes -5; scriptPubKey has OP_ABS 5 OP_NUMEQUAL
    // Stack after scriptSig: [-5]
    // After OP_ABS: [5]
    // After 5: [5, 5]
    // After OP_NUMEQUAL: [1] — pass
    tests.push_back(TestBuilder(colored(CScript() << OP_ABS << 5 << OP_NUMEQUAL),
        "Colored OP_ABS valid (|-5|=5)", 0)
                        .Num(-5));
    tests.push_back(TestBuilder(colored(CScript() << OP_ABS << 5 << OP_NUMEQUAL),
        "Colored OP_ABS invalid (|-3|=3 != 5)", 0)
                        .Num(-3)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    // OP_NEGATE: scriptSig pushes -5; scriptPubKey has OP_NEGATE 5 OP_NUMEQUAL
    // Stack after scriptSig: [-5]
    // After OP_NEGATE: [5]
    // After 5: [5, 5]
    // After OP_NUMEQUAL: [1] — pass
    tests.push_back(TestBuilder(colored(CScript() << OP_NEGATE << 5 << OP_NUMEQUAL),
        "Colored OP_NEGATE valid (-(-5)=5)", 0)
                        .Num(-5));
    tests.push_back(TestBuilder(colored(CScript() << OP_NEGATE << 5 << OP_NUMEQUAL),
        "Colored OP_NEGATE invalid (-(-3)=3 != 5)", 0)
                        .Num(-3)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    // OP_HASH160 preimage: scriptSig pushes preimage; scriptPubKey checks HASH160
    // preimage = {0x61, 0x62, 0x63} = "abc"
    {
        std::vector<unsigned char> preimage = {'a', 'b', 'c'};
        uint160 h = Hash160(preimage);
        tests.push_back(TestBuilder(colored(CScript() << OP_HASH160 << ToByteVector(h) << OP_EQUAL),
            "Colored OP_HASH160 preimage valid", 0)
                            .Add(CScript() << preimage));
        std::vector<unsigned char> wrongPreimage = {'x', 'y', 'z'};
        tests.push_back(TestBuilder(colored(CScript() << OP_HASH160 << ToByteVector(h) << OP_EQUAL),
            "Colored OP_HASH160 wrong preimage", 0)
                            .Add(CScript() << wrongPreimage)
                            .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    }

    // OP_SIZE: scriptSig pushes a 4-byte blob; scriptPubKey checks size==4 then drops blob
    // Stack after scriptSig: [{4-byte blob}]
    // After OP_SIZE: [{4-byte blob}, 4]
    // After 4: [{4-byte blob}, 4, 4]
    // After OP_EQUALVERIFY: [{4-byte blob}]
    // After OP_DROP: []
    // After OP_1: [1] — pass
    {
        std::vector<unsigned char> blob4 = {'a', 'b', 'c', 'd'};
        std::vector<unsigned char> blob2 = {'a', 'b'};
        tests.push_back(TestBuilder(colored(CScript() << OP_SIZE << 4 << OP_EQUALVERIFY << OP_DROP << OP_1),
            "Colored OP_SIZE valid (4-byte blob)", 0)
                            .Add(CScript() << blob4));
        tests.push_back(TestBuilder(colored(CScript() << OP_SIZE << 4 << OP_EQUALVERIFY << OP_DROP << OP_1),
            "Colored OP_SIZE invalid (2-byte blob, size mismatch)", 0)
                            .Add(CScript() << blob2)
                            .ScriptError(SCRIPT_ERR_EQUALVERIFY));
    }

    for (TestBuilder& test : tests) {
        test.Test();
    }
}

BOOST_AUTO_TEST_CASE(colored_coin_opcolor_validity)
{
    const KeyData keys;

    // Valid colorIds — non-zero payload
    ColorIdentifier reissuableCid(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG);
    COutPoint outpoint(uint256S("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"), 0);
    ColorIdentifier nonReissuableCid(outpoint, TokenTypes::NON_REISSUABLE);
    ColorIdentifier nftCid(outpoint, TokenTypes::NFT);

    // Invalid 33-byte colorId: type byte 0xc4 (unsupported by Tapyrus)
    std::vector<unsigned char> invalidC4Cid(33);
    invalidC4Cid[0] = 0xc4;
    for (int i = 1; i < 33; i++) invalidC4Cid[i] = static_cast<unsigned char>(i); // non-zero payload

    // Helper to concatenate <colorId> OP_COLOR in front of a suffix script
    auto colored = [](const ColorIdentifier& cid, CScript suffix) {
        CScript s;
        s << cid.toVector() << OP_COLOR;
        s += suffix;
        return s;
    };

    std::vector<TestBuilder> tests;

    // ==========================================================
    // 1. OP_WITHIN + OP_COLOR: check that a pushed value falls in the
    //    valid colorId type-byte range [0xc1, 0xc4).
    //    ScriptPubKey: <cid> OP_COLOR 0xc1 0xc4 OP_WITHIN
    //    ScriptSig:    Num(x)
    //    Execution after OP_COLOR: [x]. After 0xc1=193, 0xc4=196: [x,193,196].
    //    OP_WITHIN → [1] if 193 <= x < 196, else [0].
    // ==========================================================
    // Valid: x = 0xc1 (193, REISSUABLE type byte) — in range
    tests.push_back(TestBuilder(colored(reissuableCid, CScript() << 193 << 196 << OP_WITHIN),
        "OP_WITHIN + OP_COLOR: type byte in range (x=0xc1)", 0)
                        .Num(193));
    // Valid: x = 0xc2 (194, NON_REISSUABLE) — in range
    tests.push_back(TestBuilder(colored(reissuableCid, CScript() << 193 << 196 << OP_WITHIN),
        "OP_WITHIN + OP_COLOR: type byte in range (x=0xc2)", 0)
                        .Num(194));
    // Valid: x = 0xc3 (195, NFT) — in range
    tests.push_back(TestBuilder(colored(reissuableCid, CScript() << 193 << 196 << OP_WITHIN),
        "OP_WITHIN + OP_COLOR: type byte in range (x=0xc3)", 0)
                        .Num(195));
    // Invalid: x = 0xc4 (196, unsupported) — out of range → EVAL_FALSE
    tests.push_back(TestBuilder(colored(reissuableCid, CScript() << 193 << 196 << OP_WITHIN),
        "OP_WITHIN + OP_COLOR: type byte out of range (x=0xc4)", 0)
                        .Num(196)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    // Invalid: x = 0 — out of range → EVAL_FALSE
    tests.push_back(TestBuilder(colored(reissuableCid, CScript() << 193 << 196 << OP_WITHIN),
        "OP_WITHIN + OP_COLOR: type byte out of range (x=0)", 0)
                        .Num(0)
                        .ScriptError(SCRIPT_ERR_EVAL_FALSE));

    // ==========================================================
    // 2. OP_SWAP double-swap mischief: valid colorId comes from scriptSig.
    //
    //    ScriptSig:   <validCid>
    //    ScriptPubKey: <invalidC4> OP_SWAP OP_COLOR OP_1 OP_SWAP OP_COLOR OP_1
    //
    //    Execution:
    //      scriptSig pushes:      [validCid]
    //      <invalidC4>:           [validCid, invalidC4]   (c4 = TOS)
    //      OP_SWAP (1st):         [invalidC4, validCid]   (validCid = TOS)
    //        → valid colorId is now at TOS as intended
    //      OP_COLOR:              color set with validCid → [invalidC4]
    //      OP_1:                  [invalidC4, 1]           (dummy for 2nd swap)
    //      OP_SWAP (2nd):         [1, invalidC4]           (c4 = TOS)
    //        → attacker tries to issue token with invalid c4 colorId
    //      OP_COLOR:              color already set → SCRIPT_ERR_OP_COLORMULTIPLE
    //
    //    The mischief fails because OP_COLOR can only be set once per script
    //    execution; the COLORMULTIPLE check fires before c4 validity is tested.
    // ==========================================================
    {
        CScript doubleSwapMischief;
        doubleSwapMischief << invalidC4Cid   // push c4 into scriptPubKey
                           << OP_SWAP        // 1st swap: bring valid cid from scriptSig to TOS
                           << OP_COLOR       // set color with valid cid
                           << OP_1           // push dummy so 2nd swap has two items
                           << OP_SWAP        // 2nd swap: bring c4 to TOS
                           << OP_COLOR       // attempt: COLORMULTIPLE (color already set)
                           << OP_1;
        tests.push_back(TestBuilder(doubleSwapMischief,
            "OP_SWAP x2: valid cid from scriptSig sets color, c4 swapped to TOS for 2nd OP_COLOR → COLORMULTIPLE", 0)
                            .Add(CScript() << reissuableCid.toVector())
                            .ScriptError(SCRIPT_ERR_OP_COLORMULTIPLE));

        // Contrast — single swap: scriptSig provides valid colorId; scriptPubKey swaps
        // c4 out of the way so valid cid is TOS for OP_COLOR, c4 dropped afterwards.
        //   scriptSig:    [validCid]
        //   <invalidC4>:  [validCid, invalidC4]
        //   OP_SWAP:      [invalidC4, validCid]   validCid = TOS
        //   OP_COLOR:     color set → [invalidC4]
        //   OP_DROP:      []
        //   OP_1:         [1] ✓
        CScript swapValid;
        swapValid << invalidC4Cid << OP_SWAP << OP_COLOR << OP_DROP << OP_1;
        tests.push_back(TestBuilder(swapValid,
            "OP_SWAP single: valid cid from scriptSig swapped to TOS, c4 dropped → success", 0)
                            .Add(CScript() << reissuableCid.toVector()));
    }

    // ==========================================================
    // 2b. OP_SWAP double-swap: both colorIds valid but different types.
    //
    //    ScriptSig:    <nftCid(c3)>       (valid NFT colorId — supplied by spender)
    //    ScriptPubKey: <reissuableCid(c1)> OP_SWAP OP_COLOR OP_1 OP_SWAP OP_COLOR OP_1
    //
    //    GetColorIdFromScript → NONE: not CP2PKH, not CP2SH.
    //    Any IsColoredScript() true that doesn't match CP2PKH or CP2SH is rejected
    //    at transaction validation with "bad-txns-nonstandard-opcolor".
    //
    //    Runtime execution:
    //      scriptSig pushes:        [nftCid]
    //      <reissuableCid>:         [nftCid, reissuableCid]   (c1 = TOS)
    //      OP_SWAP (1st):           [reissuableCid, nftCid]   (nftCid = TOS)
    //        → spender's NFT colorId is brought to TOS for OP_COLOR
    //      OP_COLOR:                color set with nftCid → [reissuableCid]
    //      OP_1:                    [reissuableCid, 1]
    //      OP_SWAP (2nd):           [1, reissuableCid]         (c1 = TOS)
    //      OP_COLOR:                color already set → SCRIPT_ERR_OP_COLORMULTIPLE
    //
    //    The COLORMULTIPLE check fires before the c1 colorId is ever evaluated.
    //    More importantly, CheckColorIdentifierValidity rejects any output locked
    //    by this scriptPubKey with "bad-txns-mischievous-opcolor" — so this UTXO
    //    can never be created on-chain in the first place.
    // ==========================================================
    {
        CScript doubleSwapValidCids;
        doubleSwapValidCids << reissuableCid.toVector()  // c1 in scriptPubKey (decoy)
                            << OP_SWAP                   // 1st swap: bring c3 from scriptSig to TOS
                            << OP_COLOR                  // sets NFT color from scriptSig
                            << OP_1
                            << OP_SWAP                   // 2nd swap: bring c1 to TOS
                            << OP_COLOR                  // attempt: COLORMULTIPLE
                            << OP_1;
        tests.push_back(TestBuilder(doubleSwapValidCids,
            "OP_SWAP x2: c3(NFT) from scriptSig sets color; c1(REISSUABLE) in scriptPubKey is decoy → COLORMULTIPLE", 0)
                            .Add(CScript() << nftCid.toVector())
                            .ScriptError(SCRIPT_ERR_OP_COLORMULTIPLE));

        // Contrast — single swap, both valid colorIds:
        //   scriptSig provides c3(NFT); scriptPubKey embeds c1(REISSUABLE) as decoy.
        //   OP_SWAP brings c3 to TOS for OP_COLOR; c1 is then dropped.
        //
        //   scriptSig pushes:        [nftCid]
        //   <reissuableCid>:         [nftCid, reissuableCid]   (c1 = TOS)
        //   OP_SWAP:                 [reissuableCid, nftCid]   (nftCid = TOS)
        //   OP_COLOR:                color set with nftCid(c3) → [reissuableCid]
        //   OP_DROP:                 []
        //   OP_1:                    [1] → success
        //
        //   GetColorIdFromScript=NONE (not CP2PKH/CP2SH). The script passes raw
        //   execution but CheckColorIdentifierValidity rejects any output locked by
        //   this script with "bad-txns-nonstandard-opcolor".
        CScript swapValidCids;
        swapValidCids << reissuableCid.toVector() << OP_SWAP << OP_COLOR << OP_DROP << OP_1;
        tests.push_back(TestBuilder(swapValidCids,
            "OP_SWAP single: c3(NFT) from scriptSig swapped to TOS, c1(REISSUABLE) decoy dropped → success", 0)
                            .Add(CScript() << nftCid.toVector()));
    }

    // ==========================================================
    // 3. OP_ROT + OP_COLOR:
    //    OP_ROT rotates [a, b, c] → [b, c, a] (a becomes TOS).
    //
    //    Valid: colorId is at the bottom; OP_ROT brings it to TOS for OP_COLOR.
    //    Mischievous: invalid c4 colorId at bottom; OP_ROT brings it to TOS.
    // ==========================================================
    {
        // Valid: <validCid> OP_1 OP_2 OP_ROT OP_COLOR OP_DROP OP_DROP OP_1
        // Stack: [validCid, 1, 2]
        // OP_ROT → [1, 2, validCid]  validCid=TOS
        // OP_COLOR: valid → [1, 2]
        // OP_DROP OP_DROP → []   OP_1 → [1] ✓
        CScript rotValid;
        rotValid << reissuableCid.toVector() << OP_1 << OP_2
                 << OP_ROT << OP_COLOR << OP_DROP << OP_DROP << OP_1;
        tests.push_back(TestBuilder(rotValid,
            "OP_ROT + OP_COLOR valid: colorId rotated from bottom to TOS", 0));

        // Mischievous: <invalidC4> <validCid> OP_1 OP_ROT OP_COLOR
        // Stack: [invalidC4, validCid, 1]
        // OP_ROT → [validCid, 1, invalidC4]  invalidC4=TOS
        // OP_COLOR: type=0xc4 → SCRIPT_ERR_OP_COLORID_INVALID
        CScript rotAttack;
        rotAttack << invalidC4Cid << reissuableCid.toVector() << OP_1
                  << OP_ROT << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(rotAttack,
            "OP_ROT attack: invalid c4 colorId rotated to TOS for OP_COLOR", 0)
                            .ScriptError(SCRIPT_ERR_OP_COLORID_INVALID));

        // Variant: replace invalid c4 with valid c3(NFT). Both colorIds are embedded
        // in scriptPubKey; OP_ROT brings nftCid to TOS for OP_COLOR.
        //
        //   Stack: [nftCid, reissuableCid, 1]
        //   OP_ROT → [reissuableCid, 1, nftCid]   nftCid = TOS
        //   OP_COLOR: valid NFT → [reissuableCid, 1]
        //   OP_1:    [reissuableCid, 1, 1] → success
        //
        //   GetColorIdFromScript → NONE (not CP2PKH, not CP2SH).
        //   Transaction validation rejects any output with this script:
        //   "bad-txns-nonstandard-opcolor".
        CScript rotValidC3;
        rotValidC3 << nftCid.toVector() << reissuableCid.toVector() << OP_1
                   << OP_ROT << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(rotValidC3,
            "OP_ROT: c3(NFT) at bottom, rotated to TOS for OP_COLOR; c1 decoy left on stack → success", 0));
    }

    // ==========================================================
    // 4. OP_TOALTSTACK + OP_COLOR: mischievous "thin air" token issuance.
    //
    //    a) Move valid colorId to altstack → OP_COLOR on empty main stack
    //       → SCRIPT_ERR_INVALID_STACK_OPERATION (stack underflow).
    //    b) OP_FROMALTSTACK on empty altstack before OP_COLOR
    //       → SCRIPT_ERR_INVALID_ALTSTACK_OPERATION.
    //    c) ScriptSig pushes invalid c4 colorId; script uses OP_TOALTSTACK /
    //       OP_FROMALTSTACK round-trip → OP_COLOR still sees invalid type
    //       → SCRIPT_ERR_OP_COLORID_INVALID.
    //    d) Valid round-trip: scriptSig pushes valid colorId through altstack,
    //       then OP_COLOR succeeds.
    // ==========================================================
    {
        // (a) Valid colorId moved to altstack; OP_COLOR on empty main stack
        CScript toAltThenColor;
        toAltThenColor << reissuableCid.toVector() << OP_TOALTSTACK << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(toAltThenColor,
            "OP_TOALTSTACK attack: colorId hidden in altstack; OP_COLOR on empty main stack", 0)
                            .ScriptError(SCRIPT_ERR_INVALID_STACK_OPERATION));

        // (b) OP_FROMALTSTACK on empty altstack
        CScript fromEmptyAlt;
        fromEmptyAlt << OP_FROMALTSTACK << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(fromEmptyAlt,
            "OP_FROMALTSTACK on empty altstack before OP_COLOR", 0)
                            .ScriptError(SCRIPT_ERR_INVALID_ALTSTACK_OPERATION));

        // (c) ScriptSig pushes invalid c4 colorId; altstack round-trip does not fix type
        CScript altRoundTripInvalid;
        altRoundTripInvalid << OP_TOALTSTACK << OP_FROMALTSTACK << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(altRoundTripInvalid,
            "OP_TOALTSTACK round-trip with invalid c4 colorId from scriptSig", 0)
                            .Add(CScript() << invalidC4Cid)
                            .ScriptError(SCRIPT_ERR_OP_COLORID_INVALID));

        // (d) Valid: scriptSig pushes valid colorId; round-trip through altstack succeeds
        CScript altRoundTripValid;
        altRoundTripValid << OP_TOALTSTACK << OP_FROMALTSTACK << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(altRoundTripValid,
            "OP_TOALTSTACK round-trip with valid colorId from scriptSig", 0)
                            .Add(CScript() << reissuableCid.toVector()));

        // (e) NFT-decoy pattern: both c1(REISSUABLE) and c3(NFT) in scriptPubKey.
        //     c3 is stashed in altstack before OP_COLOR; c1 is consumed by OP_COLOR;
        //     c3 is retrieved as the final truthy TOS value.
        //
        //     ScriptPubKey: <c1> <c3> OP_TOALTSTACK OP_COLOR OP_FROMALTSTACK
        //     ScriptSig:    (empty)
        //
        //     Execution:
        //       push c1          → main: [c1],      alt: []
        //       push c3          → main: [c1, c3],  alt: []
        //       OP_TOALTSTACK    → main: [c1],       alt: [c3]
        //       OP_COLOR         → color=c1(REISSUABLE), main: [], alt: [c3]
        //       OP_FROMALTSTACK  → main: [c3],       alt: []
        //       TOS = c3 (0xc3… → truthy) → success
        //
        //     Color at runtime: c1 (REISSUABLE). NFT checks never apply.
        //     c3(NFT) colorId is embedded but only serves as the truthy final value.
        //
        //     GetColorIdFromScript → NONE (not CP2PKH, not CP2SH).
        //     CheckColorIdentifierValidity rejects any output locked by this script
        //     with "bad-txns-nonstandard-opcolor".
        CScript altDecoyNFT;
        altDecoyNFT << reissuableCid.toVector() << nftCid.toVector()
                    << OP_TOALTSTACK << OP_COLOR << OP_FROMALTSTACK;
        tests.push_back(TestBuilder(altDecoyNFT,
            "OP_TOALTSTACK NFT-decoy: c3 stashed in altstack, c1 consumed by OP_COLOR, c3 returned as truthy TOS → success", 0));
    }

    // ==========================================================
    // 5. OP_DROP + OP_COLOR: drop the colorId then try OP_COLOR.
    //
    //    a) Valid colorId dropped → OP_COLOR on empty stack
    //       → SCRIPT_ERR_INVALID_STACK_OPERATION.
    //    b) Valid colorId dropped; invalid c4 data inserted; OP_COLOR sees c4
    //       → SCRIPT_ERR_OP_COLORID_INVALID.
    //    c) ScriptSig pushes something, script drops it and inserts invalid c4
    //       → SCRIPT_ERR_OP_COLORID_INVALID.
    // ==========================================================
    {
        // (a) <validCid> OP_DROP OP_COLOR OP_1 — already tested as InvalidColoredCustomScript5
        //     in coloredScripts, but repeated here for completeness with the new check.
        CScript dropThenColor;
        dropThenColor << reissuableCid.toVector() << OP_DROP << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(dropThenColor,
            "OP_DROP + OP_COLOR: valid colorId dropped, OP_COLOR on empty stack", 0)
                            .ScriptError(SCRIPT_ERR_INVALID_STACK_OPERATION));

        // (b) <validCid> OP_DROP <invalidC4> OP_COLOR OP_1
        //     Drops valid colorId; inserts unsupported c4 bytes → OP_COLOR rejects type.
        CScript dropAndSubstitute;
        dropAndSubstitute << reissuableCid.toVector() << OP_DROP
                          << invalidC4Cid << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(dropAndSubstitute,
            "OP_DROP + OP_COLOR: valid colorId dropped, c4 data substituted → invalid type", 0)
                            .ScriptError(SCRIPT_ERR_OP_COLORID_INVALID));

        // (c) ScriptSig pushes a dummy value; script drops it and inserts c4 data.
        //     This simulates an attempt to bypass colorId checks using arbitrary data.
        CScript dropScriptSigAndSubstitute;
        dropScriptSigAndSubstitute << OP_DROP << invalidC4Cid << OP_COLOR << OP_1;
        tests.push_back(TestBuilder(dropScriptSigAndSubstitute,
            "OP_DROP + OP_COLOR: scriptSig value dropped, c4 data inserted → invalid type", 0)
                            .Num(42)
                            .ScriptError(SCRIPT_ERR_OP_COLORID_INVALID));

        // (d) NFT-decoy with OP_DROP: <c3(NFT)> <c1(REISSUABLE)> OP_COLOR OP_DROP OP_1
        //     c3 is pushed as a decoy below c1; OP_COLOR uses c1; c3 is then dropped.
        //
        //     Execution:
        //       push c3          → [c3]
        //       push c1          → [c3, c1]
        //       OP_COLOR         → color=c1(REISSUABLE), pops c1 → [c3]
        //       OP_DROP          → []
        //       OP_1             → [1] → success
        //
        //     GetColorIdFromScript → NONE (not CP2PKH, not CP2SH).
        //     CheckColorIdentifierValidity rejects with "bad-txns-nonstandard-opcolor".
        //     Script execution (tested here) still succeeds; rejection is at the
        //     transaction validation layer, not the interpreter.
        CScript dropDecoyNFT;
        dropDecoyNFT << nftCid.toVector() << reissuableCid.toVector()
                     << OP_COLOR << OP_DROP << OP_1;
        tests.push_back(TestBuilder(dropDecoyNFT,
            "OP_DROP NFT-decoy: c3 pushed below c1, OP_COLOR uses c1, c3 dropped → success (color=c1)", 0));

        // (e) Same-type decoy: two NON_REISSUABLE (c2) colorIds in scriptPubKey.
        //     One is a real c2 (payload = outpoint A); the other is a decoy c2
        //     (payload = outpoint B, not a valid issuance input for this tx).
        //
        //     ScriptPubKey: <decoy_c2> <real_c2> OP_COLOR OP_DROP OP_1
        //
        //     Execution:
        //       push decoy_c2   → [decoy_c2]
        //       push real_c2    → [decoy_c2, real_c2]
        //       OP_COLOR        → color=real_c2(NON_REISSUABLE) → [decoy_c2]
        //       OP_DROP         → []
        //       OP_1            → [1] → success
        //
        //     The decoy_c2 does NOT grant thin-air NON_REISSUABLE issuance:
        //     CheckColorIdentifierValidity requires a matching TPC input for each
        //     colored output, so decoy_c2 outputs would be rejected independently.
        //
        //     GetColorIdFromScript → NONE (not CP2PKH, not CP2SH).
        //     Transaction validation rejects the output with "bad-txns-nonstandard-opcolor".
        //     Script execution (here) still succeeds.
        COutPoint decoyOutpoint(uint256S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), 1);
        ColorIdentifier decoyNonReissuableCid(decoyOutpoint, TokenTypes::NON_REISSUABLE);
        CScript dropDecoySameType;
        dropDecoySameType << decoyNonReissuableCid.toVector() << nonReissuableCid.toVector()
                          << OP_COLOR << OP_DROP << OP_1;
        tests.push_back(TestBuilder(dropDecoySameType,
            "OP_DROP same-type decoy: two c2(NON_REISSUABLE) colorIds, OP_COLOR uses real_c2, decoy dropped → success", 0));
    }

    for (TestBuilder& test : tests) {
        test.Test();
    }
}

#if defined(HAVE_CONSENSUS_LIB)

/* Test simple (successful) usage of bitcoinconsensus_verify_script */
BOOST_AUTO_TEST_CASE(bitcoinconsensus_verify_script_returns_true)
{
    unsigned int libconsensus_flags = 0;
    int nIn = 0;

    CScript scriptPubKey;
    CScript scriptSig;
    CScriptWitness wit;

    scriptPubKey << OP_1;
    CTransaction creditTx = BuildCreditingTransaction(scriptPubKey, 1);
    CTransaction spendTx = BuildSpendingTransaction(scriptSig, wit, creditTx);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << spendTx;

    bitcoinconsensus_error err;
    int result = bitcoinconsensus_verify_script(scriptPubKey.data(), scriptPubKey.size(), (const unsigned char*)&stream[0], stream.size(), nIn, libconsensus_flags, &err);
    BOOST_CHECK_EQUAL(result, 1);
    BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_OK);
}

/* Test bitcoinconsensus_verify_script returns invalid tx index err*/
BOOST_AUTO_TEST_CASE(bitcoinconsensus_verify_script_tx_index_err)
{
    unsigned int libconsensus_flags = 0;
    int nIn = 3;

    CScript scriptPubKey;
    CScript scriptSig;
    CScriptWitness wit;

    scriptPubKey << OP_EQUAL;
    CTransaction creditTx = BuildCreditingTransaction(scriptPubKey, 1);
    CTransaction spendTx = BuildSpendingTransaction(scriptSig, wit, creditTx);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << spendTx;

    bitcoinconsensus_error err;
    int result = bitcoinconsensus_verify_script(scriptPubKey.data(), scriptPubKey.size(), (const unsigned char*)&stream[0], stream.size(), nIn, libconsensus_flags, &err);
    BOOST_CHECK_EQUAL(result, 0);
    BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_TX_INDEX);
}

/* Test bitcoinconsensus_verify_script returns tx size mismatch err*/
BOOST_AUTO_TEST_CASE(bitcoinconsensus_verify_script_tx_size)
{
    unsigned int libconsensus_flags = 0;
    int nIn = 0;

    CScript scriptPubKey;
    CScript scriptSig;
    CScriptWitness wit;

    scriptPubKey << OP_EQUAL;
    CTransaction creditTx = BuildCreditingTransaction(scriptPubKey, 1);
    CTransaction spendTx = BuildSpendingTransaction(scriptSig, wit, creditTx);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << spendTx;

    bitcoinconsensus_error err;
    int result = bitcoinconsensus_verify_script(scriptPubKey.data(), scriptPubKey.size(), (const unsigned char*)&stream[0], stream.size() * 2, nIn, libconsensus_flags, &err);
    BOOST_CHECK_EQUAL(result, 0);
    BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_TX_SIZE_MISMATCH);
}

/* Test bitcoinconsensus_verify_script returns invalid tx serialization error */
BOOST_AUTO_TEST_CASE(bitcoinconsensus_verify_script_tx_serialization)
{
    unsigned int libconsensus_flags = 0;
    int nIn = 0;

    CScript scriptPubKey;
    CScript scriptSig;
    CScriptWitness wit;

    scriptPubKey << OP_EQUAL;
    CTransaction creditTx = BuildCreditingTransaction(scriptPubKey, 1);
    CTransaction spendTx = BuildSpendingTransaction(scriptSig, wit, creditTx);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << 0xffffffff;

    bitcoinconsensus_error err;
    int result = bitcoinconsensus_verify_script(scriptPubKey.data(), scriptPubKey.size(), (const unsigned char*)&stream[0], stream.size(), nIn, libconsensus_flags, &err);
    BOOST_CHECK_EQUAL(result, 0);
    BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_TX_DESERIALIZE);
}

/* Test bitcoinconsensus_verify_script returns amount required error */
BOOST_AUTO_TEST_CASE(bitcoinconsensus_verify_script_amount_required_err)
{
    unsigned int libconsensus_flags = bitcoinconsensus_SCRIPT_FLAGS_VERIFY_WITNESS;
    int nIn = 0;

    CScript scriptPubKey;
    CScript scriptSig;
    CScriptWitness wit;

    scriptPubKey << OP_EQUAL;
    CTransaction creditTx = BuildCreditingTransaction(scriptPubKey, 1);
    CTransaction spendTx = BuildSpendingTransaction(scriptSig, wit, creditTx);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << spendTx;

    bitcoinconsensus_error err;
    int result = bitcoinconsensus_verify_script(scriptPubKey.data(), scriptPubKey.size(), (const unsigned char*)&stream[0], stream.size(), nIn, libconsensus_flags, &err);
    BOOST_CHECK_EQUAL(result, 0);
    BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_AMOUNT_REQUIRED);
}

/* Test bitcoinconsensus_verify_script returns invalid flags err */
BOOST_AUTO_TEST_CASE(bitcoinconsensus_verify_script_invalid_flags)
{
    unsigned int libconsensus_flags = 1 << 3;
    int nIn = 0;

    CScript scriptPubKey;
    CScript scriptSig;
    CScriptWitness wit;

    scriptPubKey << OP_EQUAL;
    CTransaction creditTx = BuildCreditingTransaction(scriptPubKey, 1);
    CTransaction spendTx = BuildSpendingTransaction(scriptSig, wit, creditTx);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << spendTx;

    bitcoinconsensus_error err;
    int result = bitcoinconsensus_verify_script(scriptPubKey.data(), scriptPubKey.size(), (const unsigned char*)&stream[0], stream.size(), nIn, libconsensus_flags, &err);
    BOOST_CHECK_EQUAL(result, 0);
    BOOST_CHECK_EQUAL(err, bitcoinconsensus_ERR_INVALID_FLAGS);
}

BOOST_AUTO_TEST_CASE(coloredScripts)
{
    std::vector<unsigned char> data;
    std::vector<unsigned char> colorId;

    // CP2PKH(Colored P2PKH)：
    // <COLOR identifier> OP_COLOR OP_DUP OP_HASH160 <H(pubkey)> OP_EQUALVERIFY OP_CHECKSIG
    CScript CP2PKHScriptPubKey = CScript() << ParseHex("c11863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262") << OP_COLOR << OP_DUP << OP_HASH160 << ParseHex("1018853670f9f3b0582c5b9ee8ce93764ac32b93") << OP_EQUALVERIFY << OP_CHECKSIG;

    //check CP2PKHScriptPubKey match ColoredPayToPubkeyHash not match ColoredPayToScriptHash
    BOOST_CHECK(CP2PKHScriptPubKey.IsColoredPayToPubkeyHash(data, colorId));
    BOOST_CHECK(!CP2PKHScriptPubKey.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(CP2PKHScriptPubKey).type == TokenTypes::REISSUABLE);
    
    // CP2SH(Colored P2SH)：
    // <COLOR identifier> OP_COLOR OP_HASH160 <H(redeem script)> OP_EQUAL
    CScript CP2SHScriptPubKey = CScript() << ParseHex("c11863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262") << OP_COLOR << OP_HASH160 << ParseHex("da8a647bba351bbae4cee0089d373c97ec240580") << OP_EQUAL;

    //check CP2SHScriptPubKey match ColoredPayToScriptHash not match ColoredPayToPubkeyHash
    BOOST_CHECK(!CP2SHScriptPubKey.IsColoredPayToPubkeyHash(data, colorId));
    BOOST_CHECK(CP2SHScriptPubKey.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(CP2SHScriptPubKey).type == TokenTypes::REISSUABLE);

    //check ScriptPubKey will not match ColoredPayToScriptHash and ColoredPayToPubkeyHash will return token type none
    CScript ScriptPubKey = CScript() << OP_HASH160 << ParseHex("f194d154e64f22611bc67e906e5f8fd72e6afcf1") << OP_EQUAL;
    BOOST_CHECK(!ScriptPubKey.IsColoredPayToPubkeyHash(data, colorId));
    BOOST_CHECK(!ScriptPubKey.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(ScriptPubKey).type == TokenTypes::NONE);

    // CP2SH(Colored P2SH)：
    // <COLOR identifier> OP_COLOR OP_HASH160 <H(redeem script)> OP_EQUAL
    // TokenType NON_REISSUABLE
    CScript ColoredPayToScriptHash2 = CScript() << ParseHex("c11863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262") << OP_COLOR << OP_DUP << OP_HASH160 << ParseHex("da8a647bba351bbae4cee0089d373c97ec240580") << OP_EQUALVERIFY << OP_CHECKSIG;
    BOOST_CHECK(ColoredPayToScriptHash2.IsColoredPayToPubkeyHash(data, colorId));
    BOOST_CHECK(!ColoredPayToScriptHash2.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(ColoredPayToScriptHash2).type == TokenTypes::REISSUABLE);

    // Custom script (not CP2PKH, not CP2SH): GetColorIdFromScript returns NONE.
    CScript ColoredCustomScript = CScript() << ParseHex("c11863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262") << OP_COLOR << OP_9 << OP_ADD << OP_11 << OP_EQUAL;
    BOOST_CHECK(!ColoredCustomScript.IsColoredPayToScriptHash());
    BOOST_CHECK(ColoredCustomScript.IsColoredScript());
    BOOST_CHECK(GetColorIdFromScript(ColoredCustomScript).type == TokenTypes::NONE);

    // -- All-zero payload colorIds (type byte only, payload = 32 zero bytes) --
    // Verify IsColoredScript() == true and GetColorIdFromScript() returns the correct type
    // for CP2PKH and CP2SH; custom colored scripts return NONE.

    auto zeroColorIdVec = [](uint8_t typeByte) {
        std::vector<unsigned char> v(COLOR_IDENTIFIER_SIZE, 0x00);
        v[0] = typeByte;
        return v;
    };

    const std::vector<unsigned char> cidBytesR  = zeroColorIdVec(TokenToUint(TokenTypes::REISSUABLE));
    const std::vector<unsigned char> cidBytesNR = zeroColorIdVec(TokenToUint(TokenTypes::NON_REISSUABLE));
    const std::vector<unsigned char> cidBytesNFT= zeroColorIdVec(TokenToUint(TokenTypes::NFT));

    const std::vector<unsigned char> pubkeyhash20(20, 0x00);
    const std::vector<unsigned char> scripthash20(20, 0x00);

    auto makeCP2PKH = [&](const std::vector<unsigned char>& cidBytes) {
        return CScript() << cidBytes << OP_COLOR << OP_DUP << OP_HASH160 << pubkeyhash20 << OP_EQUALVERIFY << OP_CHECKSIG;
    };
    auto makeCP2SH = [&](const std::vector<unsigned char>& cidBytes) {
        return CScript() << cidBytes << OP_COLOR << OP_HASH160 << scripthash20 << OP_EQUAL;
    };
    auto makeCustom = [&](const std::vector<unsigned char>& cidBytes) {
        return CScript() << cidBytes << OP_COLOR << OP_9 << OP_ADD << OP_11 << OP_EQUAL;
    };

    for (const auto& [cidBytes, expectedType] : std::vector<std::pair<std::vector<unsigned char>, TokenTypes>>{
        {cidBytesR,   TokenTypes::REISSUABLE},
        {cidBytesNR,  TokenTypes::NON_REISSUABLE},
        {cidBytesNFT, TokenTypes::NFT},
    }) {
        CScript cp2pkh  = makeCP2PKH(cidBytes);
        CScript cp2sh   = makeCP2SH(cidBytes);
        CScript custom  = makeCustom(cidBytes);

        BOOST_CHECK(cp2pkh.IsColoredScript());
        BOOST_CHECK(cp2sh.IsColoredScript());
        BOOST_CHECK(custom.IsColoredScript());

        // CP2PKH and CP2SH resolve to the correct token type.
        BOOST_CHECK(GetColorIdFromScript(cp2pkh).type  == expectedType);
        BOOST_CHECK(GetColorIdFromScript(cp2sh).type   == expectedType);
        BOOST_CHECK(GetColorIdFromScript(cp2pkh).toVector()  == cidBytes);
        BOOST_CHECK(GetColorIdFromScript(cp2sh).toVector()   == cidBytes);

        // Custom colored scripts are not permitted → NONE.
        BOOST_CHECK(GetColorIdFromScript(custom).type  == TokenTypes::NONE);
    }
}

BOOST_AUTO_TEST_CASE(colored_coin_combined_scripts)
{
    const KeyData keys;

    // REISSUABLE colorId for all tests
    ColorIdentifier reissuableCid(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG);

    // NON_REISSUABLE and NFT colorIds from a fixed outpoint
    COutPoint outpoint(uint256S("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"), 0);
    ColorIdentifier nonReissuableCid(outpoint, TokenTypes::NON_REISSUABLE);
    ColorIdentifier nftCid(outpoint, TokenTypes::NFT);

    // Helper: wrap a scriptPubKey suffix with <colorId> OP_COLOR
    auto colored = [](const ColorIdentifier& cid, CScript suffix) {
        CScript s;
        s << cid.toVector() << OP_COLOR;
        s += suffix;
        return s;
    };

    std::vector<TestBuilder> tests;

    // =========================================================
    // OP_CHECKDATASIG + OP_COLOR
    // Script: <colorId> OP_COLOR <pubkey> OP_CHECKDATASIG
    // Stack expected by OP_CHECKDATASIG: [sig, message, pubkey]
    // ScriptSig pushes: sig (deepest), message (TOS before scriptPubKey)
    // After colorId OP_COLOR, scriptPubKey pushes pubkey → [sig, message, pubkey]
    // =========================================================
    {
        std::vector<uint8_t> msg = {};  // empty message

        auto cdsScript = [&](const ColorIdentifier& cid) {
            return colored(cid, CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKDATASIG);
        };

        // REISSUABLE ECDSA — valid
        tests.push_back(TestBuilder(cdsScript(reissuableCid),
            "Colored OP_CHECKDATASIG REISSUABLE ECDSA", 0)
                            .PushDataSig(keys.key1, SignatureScheme::ECDSA, msg)
                            .Add(CScript() << msg));
        // REISSUABLE SCHNORR — valid
        tests.push_back(TestBuilder(cdsScript(reissuableCid),
            "Colored OP_CHECKDATASIG REISSUABLE SCHNORR", 0)
                            .PushDataSig(keys.key1, SignatureScheme::SCHNORR, msg)
                            .Add(CScript() << msg));
        // NON_REISSUABLE ECDSA — valid
        tests.push_back(TestBuilder(cdsScript(nonReissuableCid),
            "Colored OP_CHECKDATASIG NON_REISSUABLE ECDSA", 0)
                            .PushDataSig(keys.key1, SignatureScheme::ECDSA, msg)
                            .Add(CScript() << msg));
        // NFT ECDSA — valid
        tests.push_back(TestBuilder(cdsScript(nftCid),
            "Colored OP_CHECKDATASIG NFT ECDSA", 0)
                            .PushDataSig(keys.key1, SignatureScheme::ECDSA, msg)
                            .Add(CScript() << msg));
        // Wrong key — sig fails, EVAL_FALSE
        tests.push_back(TestBuilder(cdsScript(reissuableCid),
            "Colored OP_CHECKDATASIG wrong key", 0)
                            .PushDataSig(keys.key2, SignatureScheme::ECDSA, msg)
                            .Add(CScript() << msg)
                            .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    }

    // =========================================================
    // OP_CHECKMULTISIG + OP_COLOR
    // Script: <colorId> OP_COLOR OP_1 <pubkey1> <pubkey2> OP_2 OP_CHECKMULTISIG
    // 1-of-2 multisig; ScriptSig: OP_0 (dummy) <sig>
    // =========================================================
    {
        auto multisigScript = [&](const ColorIdentifier& cid) {
            return colored(cid, CScript() << OP_1
                                          << ToByteVector(keys.pubkey1C)
                                          << ToByteVector(keys.pubkey2C)
                                          << OP_2 << OP_CHECKMULTISIG);
        };

        // REISSUABLE, sig from key1 — valid
        tests.push_back(TestBuilder(multisigScript(reissuableCid),
            "Colored OP_CHECKMULTISIG REISSUABLE sig key1", 0)
                            .Num(0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA));
        // REISSUABLE, sig from key2 — valid (key2 is second pubkey)
        tests.push_back(TestBuilder(multisigScript(reissuableCid),
            "Colored OP_CHECKMULTISIG REISSUABLE sig key2", 0)
                            .Num(0)
                            .PushSig(keys.key2, SignatureScheme::ECDSA));
        // NON_REISSUABLE, sig from key1 — valid
        tests.push_back(TestBuilder(multisigScript(nonReissuableCid),
            "Colored OP_CHECKMULTISIG NON_REISSUABLE sig key1", 0)
                            .Num(0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA));
        // NFT, sig from key1 — valid
        tests.push_back(TestBuilder(multisigScript(nftCid),
            "Colored OP_CHECKMULTISIG NFT sig key1", 0)
                            .Num(0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA));
        // REISSUABLE Schnorr, sig from key1 — valid
        tests.push_back(TestBuilder(multisigScript(reissuableCid),
            "Colored OP_CHECKMULTISIG REISSUABLE SCHNORR sig key1", 0)
                            .Num(0)
                            .PushSig(keys.key1, SignatureScheme::SCHNORR));
        // Wrong key (key0 not in script) — EVAL_FALSE
        tests.push_back(TestBuilder(multisigScript(reissuableCid),
            "Colored OP_CHECKMULTISIG wrong key", 0)
                            .Num(0)
                            .PushSig(keys.key0, SignatureScheme::ECDSA)
                            .ScriptError(SCRIPT_ERR_EVAL_FALSE));
    }

    // =========================================================
    // OP_IF + OP_COLOR — conditional colored scripts
    // Script: <colorId> OP_COLOR OP_IF <pubkey1C> OP_CHECKSIG OP_ELSE <pubkey2C> OP_CHECKSIG OP_ENDIF
    // OP_COLOR is at top level (before OP_IF), which is valid.
    // Branch A (condition=1): unlocked by sig from key1
    // Branch B (condition=0): unlocked by sig from key2
    // These represent conditional issue, transfer, or burn — same script, different spenders.
    // =========================================================
    {
        auto ifScript = [&](const ColorIdentifier& cid) {
            return colored(cid, CScript() << OP_IF
                                          << ToByteVector(keys.pubkey1C) << OP_CHECKSIG
                                          << OP_ELSE
                                          << ToByteVector(keys.pubkey2C) << OP_CHECKSIG
                                          << OP_ENDIF);
        };

        // REISSUABLE branch A (condition=1, key1) — valid
        tests.push_back(TestBuilder(ifScript(reissuableCid),
            "Colored OP_IF REISSUABLE branch A (key1, condition=1) ECDSA", 0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA)
                            .Num(1));
        tests.push_back(TestBuilder(ifScript(reissuableCid),
            "Colored OP_IF REISSUABLE branch A (key1, condition=1) SCHNORR", 0)
                            .PushSig(keys.key1, SignatureScheme::SCHNORR)
                            .Num(1));
        // REISSUABLE branch B (condition=0, key2) — valid
        tests.push_back(TestBuilder(ifScript(reissuableCid),
            "Colored OP_IF REISSUABLE branch B (key2, condition=0) ECDSA", 0)
                            .PushSig(keys.key2, SignatureScheme::ECDSA)
                            .Num(0));
        tests.push_back(TestBuilder(ifScript(reissuableCid),
            "Colored OP_IF REISSUABLE branch B (key2, condition=0) SCHNORR", 0)
                            .PushSig(keys.key2, SignatureScheme::SCHNORR)
                            .Num(0));

        // NON_REISSUABLE branch A (condition=1, key1) — valid
        tests.push_back(TestBuilder(ifScript(nonReissuableCid),
            "Colored OP_IF NON_REISSUABLE branch A ECDSA", 0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA)
                            .Num(1));
        // NON_REISSUABLE branch B (condition=0, key2) — valid
        tests.push_back(TestBuilder(ifScript(nonReissuableCid),
            "Colored OP_IF NON_REISSUABLE branch B ECDSA", 0)
                            .PushSig(keys.key2, SignatureScheme::ECDSA)
                            .Num(0));

        // NFT branch A (condition=1, key1) — valid
        tests.push_back(TestBuilder(ifScript(nftCid),
            "Colored OP_IF NFT branch A ECDSA", 0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA)
                            .Num(1));
        // NFT branch B (condition=0, key2) — valid
        tests.push_back(TestBuilder(ifScript(nftCid),
            "Colored OP_IF NFT branch B ECDSA", 0)
                            .PushSig(keys.key2, SignatureScheme::ECDSA)
                            .Num(0));

        // Wrong key for branch A (key2 used but branch A expects key1) — EVAL_FALSE
        tests.push_back(TestBuilder(ifScript(reissuableCid),
            "Colored OP_IF REISSUABLE branch A wrong key", 0)
                            .PushSig(keys.key2, SignatureScheme::ECDSA)
                            .Num(1)
                            .ScriptError(SCRIPT_ERR_EVAL_FALSE));
        // Wrong key for branch B (key1 used but branch B expects key2) — EVAL_FALSE
        tests.push_back(TestBuilder(ifScript(reissuableCid),
            "Colored OP_IF REISSUABLE branch B wrong key", 0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA)
                            .Num(0)
                            .ScriptError(SCRIPT_ERR_EVAL_FALSE));

        // OP_COLOR inside IF branch — SCRIPT_ERR_OP_COLORINBRANCH (invalid placement)
        tests.push_back(TestBuilder(CScript() << OP_1 << OP_IF
                                              << reissuableCid.toVector() << OP_COLOR
                                              << ToByteVector(keys.pubkey1C) << OP_CHECKSIG
                                              << OP_ENDIF,
            "OP_COLOR inside OP_IF branch (invalid)", 0)
                            .PushSig(keys.key1, SignatureScheme::ECDSA)
                            .ScriptError(SCRIPT_ERR_OP_COLORINBRANCH));
    }

    for (TestBuilder& test : tests) {
        test.Test();
    }
}

#endif
BOOST_AUTO_TEST_SUITE_END()
