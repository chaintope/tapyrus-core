// Copyright (c) 2016-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <key.h>
#if defined(HAVE_CONSENSUS_LIB)
#include <script/tapyrusconsensus.h>
#endif
#include <script/script.h>
#include <script/sign.h>
#include <script/standard.h>
#include <streams.h>

#include <array>

// FIXME: Dedup with BuildCreditingTransaction in test/script_tests.cpp.
static CMutableTransaction BuildCreditingTransaction(const CScript& scriptPubKey)
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
    txCredit.vout[0].nValue = 1;

    return txCredit;
}

// FIXME: Dedup with BuildSpendingTransaction in test/script_tests.cpp.
static CMutableTransaction BuildSpendingTransaction(const CScript& scriptSig, const CMutableTransaction& txCredit)
{
    CMutableTransaction txSpend;
    txSpend.nFeatures = 1;
    txSpend.nLockTime = 0;
    txSpend.vin.resize(1);
    txSpend.vout.resize(1);
    txSpend.vin[0].prevout.hashMalFix = txCredit.GetHashMalFix();
    txSpend.vin[0].prevout.n = 0;
    txSpend.vin[0].scriptSig = scriptSig;
    txSpend.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    txSpend.vout[0].scriptPubKey = CScript();
    txSpend.vout[0].nValue = txCredit.vout[0].nValue;

    return txSpend;
}

// Microbenchmark for verification of a basic P2WPKH script. Can be easily
// modified to measure performance of other types of scripts.
static void VerifyScriptBench(benchmark::State& state, SignatureScheme scheme)
{
    // Keypair.
    CKey key;
    static const std::array<unsigned char, 32> vchKey = {
        {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
        }
    };
    key.Set(vchKey.begin(), vchKey.end(), false);
    CPubKey pubkey = key.GetPubKey();
    uint160 pubkeyHash;
    CHash160().Write(pubkey.begin(), pubkey.size()).Finalize(pubkeyHash.begin());

    // Script.
    CScript scriptSig;
    CScript scriptPubkey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(pubkeyHash) << OP_EQUALVERIFY << OP_CHECKSIG;
    const CMutableTransaction& txCredit = BuildCreditingTransaction(scriptPubkey);
    CMutableTransaction txSpend = BuildSpendingTransaction(scriptSig, txCredit);
    std::vector<unsigned char> vchSig;

    if(scheme == SignatureScheme::ECDSA)
        key.Sign_ECDSA(SignatureHash(scriptPubkey, txSpend, 0, SIGHASH_ALL, txCredit.vout[0].nValue, SigVersion::BASE), vchSig);
    else
        key.Sign_Schnorr(SignatureHash(scriptPubkey, txSpend, 0, SIGHASH_ALL, txCredit.vout[0].nValue, SigVersion::BASE), vchSig);

    vchSig.push_back(SIGHASH_ALL);
    txSpend.vin[0].scriptSig = CScript() << vchSig << ToByteVector(pubkey);

    // Benchmark.
    ColorIdentifier colorId;
    while (state.KeepRunning()) {
        ScriptError err;
        bool success = VerifyScript(
            txSpend.vin[0].scriptSig,
            txCredit.vout[0].scriptPubKey,
            nullptr,
            0,
            MutableTransactionSignatureChecker(&txSpend, 0, txCredit.vout[0].nValue),
            colorId,
            &err);
        assert(err == SCRIPT_ERR_OK);
        assert(success);

#if defined(HAVE_CONSENSUS_LIB)
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << txSpend;
        int csuccess = bitcoinconsensus_verify_script_with_amount(
            txCredit.vout[0].scriptPubKey.data(),
            txCredit.vout[0].scriptPubKey.size(),
            txCredit.vout[0].nValue,
            (const unsigned char*)stream.data(), stream.size(), 0, 0, nullptr);
        assert(csuccess == 1);
#endif
    }
}

static void VerifyScriptECDSABench(benchmark::State& state)
{
    VerifyScriptBench(state, SignatureScheme::ECDSA);
}

static void VerifyScriptSchnorrBench(benchmark::State& state)
{
    VerifyScriptBench(state, SignatureScheme::SCHNORR);
}

BENCHMARK(VerifyScriptECDSABench, 6300);
BENCHMARK(VerifyScriptSchnorrBench, 6300);
