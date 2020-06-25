// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
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
    tests.push_back(TestBuilder(CScript() << ColorIdentifier(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "CP2PKH ECDSA", 0)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(CScript() << ColorIdentifier(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "CP2PKH SCHNORR", 0)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C));
    tests.push_back(TestBuilder(CScript() << ColorIdentifier(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2SH(CP2PKH) ECDSA is invalid", SCRIPT_VERIFY_NONE, true, WitnessMode::NONE, 0, 0,true)
                        .PushSig(keys.key0, SignatureScheme::ECDSA)
                        .Push(keys.pubkey0)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_OP_COLOR_UNEXPECTED));
    tests.push_back(TestBuilder(CScript() << ColorIdentifier(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG).toVector() << OP_COLOR << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
        "P2SH(CP2PKH) SCHNORR is invalid", SCRIPT_VERIFY_NONE, true, WitnessMode::NONE, 0, 0,true)
                        .PushSig(keys.key0, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey0)
                        .PushRedeem()
                        .ScriptError(SCRIPT_ERR_OP_COLOR_UNEXPECTED));
    tests.push_back(TestBuilder(CScript() << ColorIdentifier(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG).toVector() << OP_COLOR << OP_HASH160 << ToByteVector(CScriptID(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG)) << OP_EQUAL,
        "CP2SH(P2PKH) ECDSA", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key1, SignatureScheme::ECDSA)
                        .Push(keys.pubkey1C)
                        .PushRedeem());
    tests.push_back(TestBuilder(CScript() << ColorIdentifier(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG).toVector() << OP_COLOR << OP_HASH160 << ToByteVector(CScriptID(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG)) << OP_EQUAL,
        "CP2SH(P2PKH) SCHNORR", SCRIPT_VERIFY_NONE, true)
                        .PushSig(keys.key1, SignatureScheme::SCHNORR)
                        .Push(keys.pubkey1C)
                        .PushRedeem());
    {
        UniValue json_tests = read_json(std::string(json_tests::script_tests, json_tests::script_tests + sizeof(json_tests::script_tests)));

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
    UniValue tests = read_json(std::string(json_tests::script_tests, json_tests::script_tests + sizeof(json_tests::script_tests)));

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
    BOOST_CHECK(combined.scriptSig == scriptSigCopy.scriptSig || combined.scriptSig == scriptSig.scriptSig);

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
    BOOST_CHECK(combined.scriptSig == scriptSigCopy.scriptSig || combined.scriptSig == scriptSig.scriptSig);

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
    BOOST_CHECK(MatchColoredPayToPubkeyHash(CP2PKHScriptPubKey, data, colorId));
    BOOST_CHECK(!CP2PKHScriptPubKey.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(CP2PKHScriptPubKey).type == TokenTypes::REISSUABLE);
    
    // CP2SH(Colored P2SH)：
    // <COLOR identifier> OP_COLOR OP_HASH160 <H(redeem script)> OP_EQUAL
    CScript CP2SHScriptPubKey = CScript() << ParseHex("c11863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262") << OP_COLOR << OP_HASH160 << ParseHex("da8a647bba351bbae4cee0089d373c97ec240580") << OP_EQUAL;

    //check CP2SHScriptPubKey match ColoredPayToScriptHash not match ColoredPayToPubkeyHash
    BOOST_CHECK(!MatchColoredPayToPubkeyHash(CP2SHScriptPubKey, data, colorId));
    BOOST_CHECK(CP2SHScriptPubKey.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(CP2SHScriptPubKey).type == TokenTypes::REISSUABLE);

    //check ScriptPubKey will not match ColoredPayToScriptHash and ColoredPayToPubkeyHash will return token type none
    CScript ScriptPubKey = CScript() << OP_HASH160 << ParseHex("f194d154e64f22611bc67e906e5f8fd72e6afcf1") << OP_EQUAL;
    BOOST_CHECK(!MatchColoredPayToPubkeyHash(ScriptPubKey, data, colorId));
    BOOST_CHECK(!ScriptPubKey.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(ScriptPubKey).type == TokenTypes::NONE);

    // CP2SH(Colored P2SH)：
    // <COLOR identifier> OP_COLOR OP_HASH160 <H(redeem script)> OP_EQUAL
    // TokenType NON_REISSUABLE
    CScript ColoredPayToScriptHash2 = CScript() << ParseHex("c11863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262") << OP_COLOR << OP_DUP << OP_HASH160 << ParseHex("da8a647bba351bbae4cee0089d373c97ec240580") << OP_EQUALVERIFY << OP_CHECKSIG;
    BOOST_CHECK(MatchColoredPayToPubkeyHash(ColoredPayToScriptHash2, data, colorId));
    BOOST_CHECK(!ColoredPayToScriptHash2.IsColoredPayToScriptHash());
    BOOST_CHECK(GetColorIdFromScript(ColoredPayToScriptHash2).type == TokenTypes::REISSUABLE);
}

#endif
BOOST_AUTO_TEST_SUITE_END()
