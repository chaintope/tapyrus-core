// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pstt.h>

#include <amount.h>
#include <chainparams.h>
#include <coloridentifier.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key.h>
#include <key_io.h>
#include <policy/policy.h>
#include <random.h>
#include <rpc/server.h>
#include <script/script.h>
#include <script/standard.h>
#include <streams.h>
#include <test/test_tapyrus.h>
#include <txmempool.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pstt_tests, BasicTestingSetup)

static CScript RandomP2PKHScript()
{
    CKey key;
    key.MakeNewKey(true);
    CKeyID id = key.GetPubKey().GetID();
    CScript script;
    script << OP_DUP << OP_HASH160 << ToByteVector(id) << OP_EQUALVERIFY << OP_CHECKSIG;
    return script;
}

static CTransactionRef MakeSimpleUtxoTx(const CScript& scriptPubKey, CAmount amount)
{
    CMutableTransaction mtx;
    mtx.nFeatures = CTransaction::CURRENT_FEATURES;
    mtx.nLockTime = 0;
    mtx.vout.emplace_back(amount, scriptPubKey);
    return MakeTransactionRef(std::move(mtx));
}

static PSTTInput MakeBasicInput(const uint256& prev_txid, uint32_t vout, const CTransactionRef& utxo)
{
    PSTTInput input;
    input.previous_txid = prev_txid;
    input.prev_out_index = vout;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    input.utxo = utxo;
    return input;
}

static PartiallySignedTapyrusTransaction MakeBasicPstt()
{
    CScript scriptPubKey = RandomP2PKHScript();
    CTransactionRef utxo = MakeSimpleUtxoTx(scriptPubKey, 100000);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    pstt.inputs.push_back(MakeBasicInput(utxo->GetHashMalFix(), 0, utxo));

    PSTTOutput output;
    output.amount = 90000;
    output.script = RandomP2PKHScript();
    pstt.outputs.push_back(output);

    return pstt;
}

template <typename T>
static std::vector<unsigned char> SerializeObj(const T& obj)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << obj;
    return std::vector<unsigned char>(ss.begin(), ss.end());
}

template <typename T>
static T UnserializeObj(const std::vector<unsigned char>& data)
{
    CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
    T obj;
    ss >> obj;
    return obj;
}

// -----------------------------------------------------------------------
// Round-trip serialize/deserialize
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_roundtrip_basic)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    std::vector<unsigned char> data = SerializeObj(pstt);
    PartiallySignedTapyrusTransaction parsed = UnserializeObj<PartiallySignedTapyrusTransaction>(data);

    BOOST_CHECK_EQUAL(*parsed.tx_features, *pstt.tx_features);
    BOOST_CHECK_EQUAL(parsed.inputs.size(), 1U);
    BOOST_CHECK_EQUAL(parsed.outputs.size(), 1U);
    BOOST_CHECK(parsed.inputs[0].previous_txid_set);
    BOOST_CHECK(parsed.inputs[0].previous_txid == pstt.inputs[0].previous_txid);
    BOOST_CHECK_EQUAL(parsed.inputs[0].prev_out_index, pstt.inputs[0].prev_out_index);
    BOOST_CHECK(parsed.inputs[0].utxo->GetHashMalFix() == pstt.inputs[0].utxo->GetHashMalFix());
    BOOST_CHECK_EQUAL(*parsed.outputs[0].amount, *pstt.outputs[0].amount);
    BOOST_CHECK(parsed.outputs[0].script == pstt.outputs[0].script);
    BOOST_CHECK(parsed.GetIdentifier() == pstt.GetIdentifier());
}

BOOST_AUTO_TEST_CASE(pstt_roundtrip_full_fields)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.fallback_locktime = 12345;
    pstt.tx_modifiable = PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE;
    pstt.version = 0;

    PSTTInput& input = pstt.inputs[0];
    input.sighash_type = 1;
    input.sequence = 0xFFFFFFFEu;
    input.required_height_locktime = 100;
    CKey key;
    key.MakeNewKey(true);
    std::vector<unsigned char> der_sig(71, 0x11);
    der_sig.push_back(1); // sighash byte
    input.partial_sigs.emplace(key.GetPubKey().GetID(), SigPair(key.GetPubKey(), der_sig));
    input.hd_keypaths.emplace(key.GetPubKey(), std::vector<uint32_t>{0x01020304, 0, 1});
    input.ripemd160_preimages.emplace(std::vector<unsigned char>(20, 0xAA), std::vector<unsigned char>{1, 2, 3});
    input.sha256_preimages.emplace(std::vector<unsigned char>(32, 0xBB), std::vector<unsigned char>{4, 5, 6});
    input.hash160_preimages.emplace(std::vector<unsigned char>(20, 0xCC), std::vector<unsigned char>{7, 8});
    input.hash256_preimages.emplace(std::vector<unsigned char>(32, 0xDD), std::vector<unsigned char>{9});
    input.unknown.emplace(std::vector<unsigned char>{(unsigned char)PSTT_IN_PROPRIETARY, 0x01}, std::vector<unsigned char>{0x42});

    std::vector<unsigned char> data = SerializeObj(pstt);
    PartiallySignedTapyrusTransaction parsed = UnserializeObj<PartiallySignedTapyrusTransaction>(data);

    BOOST_CHECK_EQUAL(*parsed.fallback_locktime, 12345U);
    BOOST_CHECK_EQUAL(*parsed.tx_modifiable, *pstt.tx_modifiable);
    BOOST_CHECK_EQUAL(*parsed.version, 0U);
    const PSTTInput& pinput = parsed.inputs[0];
    BOOST_CHECK_EQUAL(*pinput.sighash_type, 1);
    BOOST_CHECK_EQUAL(*pinput.sequence, 0xFFFFFFFEu);
    BOOST_CHECK_EQUAL(*pinput.required_height_locktime, 100U);
    BOOST_CHECK_EQUAL(pinput.partial_sigs.size(), 1U);
    BOOST_CHECK_EQUAL(pinput.hd_keypaths.size(), 1U);
    BOOST_CHECK_EQUAL(pinput.ripemd160_preimages.size(), 1U);
    BOOST_CHECK_EQUAL(pinput.sha256_preimages.size(), 1U);
    BOOST_CHECK_EQUAL(pinput.hash160_preimages.size(), 1U);
    BOOST_CHECK_EQUAL(pinput.hash256_preimages.size(), 1U);
    BOOST_CHECK_EQUAL(pinput.unknown.size(), 1U);
}

// -----------------------------------------------------------------------
// ComputeLocktime
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_computelocktime_fallback_only)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.fallback_locktime = 555;
    uint32_t locktime;
    BOOST_CHECK(ComputeLocktime(pstt, locktime));
    BOOST_CHECK_EQUAL(locktime, 555U);
}

BOOST_AUTO_TEST_CASE(pstt_computelocktime_fallback_default_zero)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    uint32_t locktime;
    BOOST_CHECK(ComputeLocktime(pstt, locktime));
    BOOST_CHECK_EQUAL(locktime, 0U);
}

BOOST_AUTO_TEST_CASE(pstt_computelocktime_height_only)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.inputs[0].required_height_locktime = 700;
    uint32_t locktime;
    BOOST_CHECK(ComputeLocktime(pstt, locktime));
    BOOST_CHECK_EQUAL(locktime, 700U);
}

BOOST_AUTO_TEST_CASE(pstt_computelocktime_time_only)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.inputs[0].required_time_locktime = 600000000;
    uint32_t locktime;
    BOOST_CHECK(ComputeLocktime(pstt, locktime));
    BOOST_CHECK_EQUAL(locktime, 600000000U);
}

BOOST_AUTO_TEST_CASE(pstt_computelocktime_both_acceptable_prefers_height)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    // Second input with no constraint at all (imposes nothing).
    CTransactionRef utxo2 = MakeSimpleUtxoTx(RandomP2PKHScript(), 50000);
    pstt.inputs.push_back(MakeBasicInput(utxo2->GetHashMalFix(), 0, utxo2));
    // First input accepts both kinds (both fields set) -> height preferred.
    pstt.inputs[0].required_height_locktime = 800;
    pstt.inputs[0].required_time_locktime = 700000000;
    uint32_t locktime;
    BOOST_CHECK(ComputeLocktime(pstt, locktime));
    BOOST_CHECK_EQUAL(locktime, 800U);
}

BOOST_AUTO_TEST_CASE(pstt_computelocktime_contradictory_kinds_fails)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    CTransactionRef utxo2 = MakeSimpleUtxoTx(RandomP2PKHScript(), 50000);
    pstt.inputs.push_back(MakeBasicInput(utxo2->GetHashMalFix(), 0, utxo2));
    // Input 0 only accepts height, input 1 only accepts time -> empty intersection.
    pstt.inputs[0].required_height_locktime = 800;
    pstt.inputs[1].required_time_locktime = 700000000;
    uint32_t locktime;
    BOOST_CHECK(!ComputeLocktime(pstt, locktime));
}

// -----------------------------------------------------------------------
// IsSane() mixed-scheme rejection
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_issane_rejects_mixed_scheme)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);

    std::vector<unsigned char> der_sig(71, 0x11);
    der_sig.push_back(1); // ECDSA-shaped: not COMPACT_SIGNATURE_SIZE (65)
    std::vector<unsigned char> schnorr_sig(CPubKey::COMPACT_SIGNATURE_SIZE, 0x22); // 65 bytes incl. sighash byte

    pstt.inputs[0].partial_sigs.emplace(key1.GetPubKey().GetID(), SigPair(key1.GetPubKey(), der_sig));
    pstt.inputs[0].partial_sigs.emplace(key2.GetPubKey().GetID(), SigPair(key2.GetPubKey(), schnorr_sig));

    BOOST_CHECK(!pstt.inputs[0].IsSane());
}

BOOST_AUTO_TEST_CASE(pstt_issane_accepts_uniform_scheme)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);

    std::vector<unsigned char> sig1(CPubKey::COMPACT_SIGNATURE_SIZE, 0x22);
    std::vector<unsigned char> sig2(CPubKey::COMPACT_SIGNATURE_SIZE, 0x33);

    pstt.inputs[0].partial_sigs.emplace(key1.GetPubKey().GetID(), SigPair(key1.GetPubKey(), sig1));
    pstt.inputs[0].partial_sigs.emplace(key2.GetPubKey().GetID(), SigPair(key2.GetPubKey(), sig2));

    BOOST_CHECK(pstt.inputs[0].IsSane());
}

// -----------------------------------------------------------------------
// Container-format edge cases
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_rejects_wrong_magic)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    std::vector<unsigned char> data = SerializeObj(pstt);
    data[0] = 'x';
    BOOST_CHECK_THROW(UnserializeObj<PartiallySignedTapyrusTransaction>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_input_rejects_missing_separator)
{
    PSTTInput input = MakeBasicInput(InsecureRand256(), 0, MakeSimpleUtxoTx(RandomP2PKHScript(), 1000));
    std::vector<unsigned char> data = SerializeObj(input);
    BOOST_REQUIRE(!data.empty());
    BOOST_REQUIRE_EQUAL(data.back(), 0x00); // PSTT_SEPARATOR
    data.pop_back(); // drop the separator -> stream ends with no terminating empty key
    BOOST_CHECK_THROW(UnserializeObj<PSTTInput>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_rejects_duplicate_key)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << PSTT_MAGIC_BYTES;
    SerializeToVector(ss, PSTT_GLOBAL_TX_FEATURES);
    SerializeToVector(ss, (int32_t)1);
    // Duplicate PSTT_GLOBAL_TX_FEATURES record.
    SerializeToVector(ss, PSTT_GLOBAL_TX_FEATURES);
    SerializeToVector(ss, (int32_t)1);
    SerializeToVector(ss, PSTT_GLOBAL_INPUT_COUNT);
    WriteCompactSize(ss, 0);
    SerializeToVector(ss, PSTT_GLOBAL_OUTPUT_COUNT);
    WriteCompactSize(ss, 0);
    ss << PSTT_SEPARATOR;

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PartiallySignedTapyrusTransaction>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_rejects_count_mismatch)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << PSTT_MAGIC_BYTES;
    SerializeToVector(ss, PSTT_GLOBAL_TX_FEATURES);
    SerializeToVector(ss, (int32_t)1);
    SerializeToVector(ss, PSTT_GLOBAL_INPUT_COUNT);
    WriteCompactSize(ss, 1); // claims 1 input
    SerializeToVector(ss, PSTT_GLOBAL_OUTPUT_COUNT);
    WriteCompactSize(ss, 0);
    ss << PSTT_SEPARATOR;
    // ... but no input map follows.

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PartiallySignedTapyrusTransaction>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_rejects_global_unsigned_tx_record)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << PSTT_MAGIC_BYTES;
    SerializeToVector(ss, (uint8_t)0x00); // reserved global key
    SerializeToVector(ss, (int32_t)1);
    ss << PSTT_SEPARATOR;

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PartiallySignedTapyrusTransaction>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_rejects_version_too_high)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << PSTT_MAGIC_BYTES;
    SerializeToVector(ss, PSTT_GLOBAL_TX_FEATURES);
    SerializeToVector(ss, (int32_t)1);
    SerializeToVector(ss, PSTT_GLOBAL_VERSION);
    SerializeToVector(ss, (uint32_t)1);
    SerializeToVector(ss, PSTT_GLOBAL_INPUT_COUNT);
    WriteCompactSize(ss, 0);
    SerializeToVector(ss, PSTT_GLOBAL_OUTPUT_COUNT);
    WriteCompactSize(ss, 0);
    ss << PSTT_SEPARATOR;

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PartiallySignedTapyrusTransaction>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_rejects_missing_required_globals)
{
    // Missing PSTT_GLOBAL_TX_FEATURES entirely.
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << PSTT_MAGIC_BYTES;
    SerializeToVector(ss, PSTT_GLOBAL_INPUT_COUNT);
    WriteCompactSize(ss, 0);
    SerializeToVector(ss, PSTT_GLOBAL_OUTPUT_COUNT);
    WriteCompactSize(ss, 0);
    ss << PSTT_SEPARATOR;

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PartiallySignedTapyrusTransaction>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_rejects_reserved_input_keytype)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    SerializeToVector(ss, (uint8_t)0x01); // reserved: witness UTXO
    ss << std::vector<unsigned char>{0x00};
    ss << PSTT_SEPARATOR;

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PSTTInput>(data), std::ios_base::failure);
}

// truncated-value: a field's declared <valuelen> exceeds the bytes actually
// remaining.
BOOST_AUTO_TEST_CASE(pstt_rejects_truncated_value)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    SerializeToVector(ss, PSTT_GLOBAL_FALLBACK_LOCKTIME);
    WriteCompactSize(ss, 4); // claims a 4-byte value...
    ss.write("\x01\x02", 2); // ...but only 2 bytes actually follow before EOF

    std::vector<unsigned char> data(ss.begin(), ss.end());
    CDataStream ss2(data, SER_NETWORK, PROTOCOL_VERSION);
    boost::optional<uint32_t> dummy;
    BOOST_CHECK_THROW({
        uint32_t v;
        UnserializeFromVector(ss2, v);
    }, std::ios_base::failure);
}

// -----------------------------------------------------------------------
// xpub round-trip
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_xpub_roundtrip_matching_prefix)
{
    // Unit tests default to TAPYRUS_OP_MODE::PROD (test_tapyrus.cpp's
    // BasicTestingSetup), so PROD's prefix is the one that must succeed here.
    CExtPubKey xpub;
    CKey key;
    key.MakeNewKey(true);
    xpub.pubkey = key.GetPubKey();
    xpub.nDepth = 1;
    xpub.nChild = 0;
    xpub.chaincode.SetNull();

    std::vector<unsigned char> keydata = SerializeXpubKeyData(xpub);
    BOOST_CHECK_EQUAL(keydata.size(), PSTT_XPUB_KEYDATA_SIZE);
    CExtPubKey parsed = ParseXpubKeyData(keydata);
    BOOST_CHECK(parsed == xpub);
}

BOOST_AUTO_TEST_CASE(pstt_xpub_rejects_wrong_network_prefix)
{
    CExtPubKey xpub;
    CKey key;
    key.MakeNewKey(true);
    xpub.pubkey = key.GetPubKey();
    xpub.nDepth = 0;
    xpub.nChild = 0;
    xpub.chaincode.SetNull();

    std::vector<unsigned char> keydata = SerializeXpubKeyData(xpub);
    // Corrupt the 4-byte prefix to the *other* real Tapyrus network's prefix
    // (DEV, since this test suite runs under PROD) -- must still throw, not
    // silently accept, per the strict Params()-only prefix policy.
    const std::vector<unsigned char>& dev_prefix = {0x04, 0x35, 0x87, 0xCF};
    std::copy(dev_prefix.begin(), dev_prefix.end(), keydata.begin());
    BOOST_CHECK_THROW(ParseXpubKeyData(keydata), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_xpub_rejects_unknown_prefix)
{
    CExtPubKey xpub;
    CKey key;
    key.MakeNewKey(true);
    xpub.pubkey = key.GetPubKey();
    xpub.nDepth = 0;
    xpub.nChild = 0;
    xpub.chaincode.SetNull();

    std::vector<unsigned char> keydata = SerializeXpubKeyData(xpub);
    keydata[0] = 0xDE;
    keydata[1] = 0xAD;
    keydata[2] = 0xBE;
    keydata[3] = 0xEF;
    BOOST_CHECK_THROW(ParseXpubKeyData(keydata), std::ios_base::failure);
}

// -----------------------------------------------------------------------
// tx_features permissive round-trip
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_tx_features_roundtrip_any_value)
{
    for (int32_t v : {0, 1, 42, INT32_MIN}) {
        PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
        pstt.tx_features = v;
        std::vector<unsigned char> data = SerializeObj(pstt);
        PartiallySignedTapyrusTransaction parsed = UnserializeObj<PartiallySignedTapyrusTransaction>(data);
        BOOST_CHECK_EQUAL(*parsed.tx_features, v);
    }
}

// -----------------------------------------------------------------------
// End-to-end valid/invalid transaction tests (mempool/consensus level)
//
// These build a PartiallySignedTapyrusTransaction, sign it via
// SignPSTTInput, and extract a final transaction the way a real Extractor
// would, then feed the result through AcceptToMemoryPool -- same pattern
// as txvalidation_tests.cpp's testTx(), but the transaction is produced by
// the PSTT pipeline (Creator/Updater/Signer/Extractor) instead of being
// hand-built with a manually constructed scriptSig.
//
// Phase 3 added a real, public Extractor (pstt.h's ExtractPSTT) -- these
// tests now call it directly instead of the test-local reconstruction that
// used to stand in for it.
// -----------------------------------------------------------------------

// Signs input `index` in place: runs SignPSTTInput, then applies
// FromSignatureData to store the result, mirroring what a real Signer RPC
// does once SignPSTTInput reports OK.
static void SignInputForTest(PartiallySignedTapyrusTransaction& pstt, unsigned int index,
                              const SigningProvider& provider, SignatureScheme scheme = SignatureScheme::ECDSA,
                              int sighash = SIGHASH_ALL)
{
    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(provider, pstt, index, sigdata, sighash, scheme);
    BOOST_REQUIRE(result == PSTTSignResult::OK);
    pstt.inputs[index].FromSignatureData(sigdata);
    BOOST_REQUIRE(sigdata.complete);
}

static void CheckMempoolResult(TestChainSetup& setup, const CTransactionRef& tx, bool expect_success,
                                const std::string& expect_reject_reason = "")
{
    CTxMempoolAcceptanceOptions opt;
    opt.flags = MempoolAcceptanceFlags::BYPASSS_LIMITS;
    {
        LOCK(cs_main);
        BOOST_CHECK_EQUAL(expect_success, AcceptToMemoryPool(tx, opt));
    }
    if (expect_success) {
        BOOST_CHECK(opt.state.IsValid());
        std::vector<CMutableTransaction> txs{CMutableTransaction(*tx)};
        setup.CreateAndProcessBlock(txs, CScript() << ToByteVector(setup.coinbaseKey.GetPubKey()) << OP_CHECKSIG);
    } else {
        BOOST_CHECK(opt.state.IsInvalid());
        if (!expect_reject_reason.empty()) {
            BOOST_CHECK_EQUAL(opt.state.GetRejectReason(), expect_reject_reason);
        }
    }
}

static CScript P2PKHScriptFor(const CKey& key)
{
    CScript script;
    script << OP_DUP << OP_HASH160 << ToByteVector(key.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    return script;
}

BOOST_FIXTURE_TEST_CASE(pstt_extract_valid_p2pk_coinbase_spend, TestChainSetup)
{
    // m_coinbase_txns[0]'s output is P2PK to coinbaseKey (TestChainSetup's own
    // construction) -- the simplest possible PSTT-signed spend.
    const CTransactionRef& prevTx = m_coinbase_txns[0];
    CKey destKey;
    destKey.MakeNewKey(true);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput input;
    input.previous_txid = prevTx->GetHashMalFix();
    input.prev_out_index = 0;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    input.utxo = prevTx;
    pstt.inputs.push_back(input);

    PSTTOutput output;
    output.amount = prevTx->vout[0].nValue - CENT;
    output.script = P2PKHScriptFor(destKey);
    pstt.outputs.push_back(output);

    FlatSigningProvider provider;
    provider.keys[coinbaseKey.GetPubKey().GetID()] = coinbaseKey;

    SignInputForTest(pstt, 0, provider);

    CheckMempoolResult(*this, MakeTransactionRef(ExtractPSTT(pstt)), true);
}

BOOST_FIXTURE_TEST_CASE(pstt_extract_valid_schnorr_spend, TestChainSetup)
{
    // Same shape as the ECDSA case above, but signed with the Schnorr scheme --
    // exercises SignPSTTInput's scheme threading end to end.
    const CTransactionRef& prevTx = m_coinbase_txns[0];
    CKey destKey;
    destKey.MakeNewKey(true);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput input;
    input.previous_txid = prevTx->GetHashMalFix();
    input.prev_out_index = 0;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    input.utxo = prevTx;
    pstt.inputs.push_back(input);

    PSTTOutput output;
    output.amount = prevTx->vout[0].nValue - CENT;
    output.script = P2PKHScriptFor(destKey);
    pstt.outputs.push_back(output);

    FlatSigningProvider provider;
    provider.keys[coinbaseKey.GetPubKey().GetID()] = coinbaseKey;

    SignInputForTest(pstt, 0, provider, SignatureScheme::SCHNORR);

    CheckMempoolResult(*this, MakeTransactionRef(ExtractPSTT(pstt)), true);
}

BOOST_FIXTURE_TEST_CASE(pstt_extract_invalid_wrong_key, TestChainSetup)
{
    // Signed with a key that doesn't match m_coinbase_txns[0]'s P2PK script --
    // ProduceSignature can't complete the script, so SignPSTTInput never
    // reaches OK; there is no extractable transaction to feed to the mempool.
    const CTransactionRef& prevTx = m_coinbase_txns[0];
    CKey wrongKey;
    wrongKey.MakeNewKey(true);
    CKey destKey;
    destKey.MakeNewKey(true);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput input;
    input.previous_txid = prevTx->GetHashMalFix();
    input.prev_out_index = 0;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    input.utxo = prevTx;
    pstt.inputs.push_back(input);

    PSTTOutput output;
    output.amount = prevTx->vout[0].nValue - CENT;
    output.script = P2PKHScriptFor(destKey);
    pstt.outputs.push_back(output);

    FlatSigningProvider provider;
    provider.keys[wrongKey.GetPubKey().GetID()] = wrongKey;

    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(provider, pstt, 0, sigdata, SIGHASH_ALL, SignatureScheme::ECDSA);
    BOOST_CHECK(result == PSTTSignResult::OK); // SignPSTTInput itself succeeds...
    BOOST_CHECK(!sigdata.complete); // ...but the script it produced doesn't satisfy scriptPubKey
}

// Builds a two-transaction chain (P2PK coinbase spend -> P2PKH) via the PSTT
// pipeline, mining each so the second's output is a confirmed, spendable
// P2PKH coin. Returns {tx, spendKey} for the confirmed P2PKH output.
static std::pair<CTransactionRef, CKey> MakeConfirmedP2PKHCoin(TestChainSetup& setup, const CTransactionRef& coinbaseTx)
{
    CKey destKey;
    destKey.MakeNewKey(true);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput input;
    input.previous_txid = coinbaseTx->GetHashMalFix();
    input.prev_out_index = 0;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    input.utxo = coinbaseTx;
    pstt.inputs.push_back(input);

    PSTTOutput output;
    output.amount = coinbaseTx->vout[0].nValue - CENT;
    output.script = P2PKHScriptFor(destKey);
    pstt.outputs.push_back(output);

    FlatSigningProvider provider;
    provider.keys[setup.coinbaseKey.GetPubKey().GetID()] = setup.coinbaseKey;
    SignInputForTest(pstt, 0, provider);

    CTransactionRef tx = MakeTransactionRef(ExtractPSTT(pstt));
    CheckMempoolResult(setup, tx, true);
    return {tx, destKey};
}

BOOST_FIXTURE_TEST_CASE(pstt_extract_valid_colored_issue_and_transfer, TestChainSetup)
{
    // Confirm a spendable P2PKH TPC coin, then issue a colored coin from it.
    CTransactionRef p2pkhTx;
    CKey issuerKey;
    std::tie(p2pkhTx, issuerKey) = MakeConfirmedP2PKHCoin(*this, m_coinbase_txns[1]);

    ColorIdentifier colorId(p2pkhTx->vout[0].scriptPubKey);
    CKey holderKey;
    holderKey.MakeNewKey(true);
    CScript coloredScript = CScript() << colorId.toVector() << OP_COLOR
                                       << OP_DUP << OP_HASH160 << ToByteVector(holderKey.GetPubKey().GetID())
                                       << OP_EQUALVERIFY << OP_CHECKSIG;

    PartiallySignedTapyrusTransaction issuePstt;
    issuePstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput issueInput;
    issueInput.previous_txid = p2pkhTx->GetHashMalFix();
    issueInput.prev_out_index = 0;
    issueInput.previous_txid_set = true;
    issueInput.prev_out_index_set = true;
    issueInput.utxo = p2pkhTx;
    issuePstt.inputs.push_back(issueInput);

    PSTTOutput issueOutput;
    issueOutput.amount = 100 * CENT;
    issueOutput.script = coloredScript;
    issuePstt.outputs.push_back(issueOutput);

    FlatSigningProvider issueProvider;
    issueProvider.keys[issuerKey.GetPubKey().GetID()] = issuerKey;
    issueProvider.pubkeys[issuerKey.GetPubKey().GetID()] = issuerKey.GetPubKey();
    SignInputForTest(issuePstt, 0, issueProvider);

    CTransactionRef issueTx = MakeTransactionRef(ExtractPSTT(issuePstt));
    CheckMempoolResult(*this, issueTx, true);

    // Transfer the colored coin, paying the TPC fee from a second coinbase spend
    // confirmed the same way -- exercises a two-input PSTT with two different
    // signing keys, matching the Fee-Provider-style multi-party shape.
    CTransactionRef feeTx;
    CKey feeKey;
    std::tie(feeTx, feeKey) = MakeConfirmedP2PKHCoin(*this, m_coinbase_txns[2]);

    CKey recipientKey;
    recipientKey.MakeNewKey(true);
    CScript recipientColoredScript = CScript() << colorId.toVector() << OP_COLOR
                                                 << OP_DUP << OP_HASH160 << ToByteVector(recipientKey.GetPubKey().GetID())
                                                 << OP_EQUALVERIFY << OP_CHECKSIG;

    PartiallySignedTapyrusTransaction transferPstt;
    transferPstt.tx_features = CTransaction::CURRENT_FEATURES;

    PSTTInput coloredInput;
    coloredInput.previous_txid = issueTx->GetHashMalFix();
    coloredInput.prev_out_index = 0;
    coloredInput.previous_txid_set = true;
    coloredInput.prev_out_index_set = true;
    coloredInput.utxo = issueTx;
    transferPstt.inputs.push_back(coloredInput);

    PSTTInput feeInput;
    feeInput.previous_txid = feeTx->GetHashMalFix();
    feeInput.prev_out_index = 0;
    feeInput.previous_txid_set = true;
    feeInput.prev_out_index_set = true;
    feeInput.utxo = feeTx;
    transferPstt.inputs.push_back(feeInput);

    PSTTOutput transferOutput;
    transferOutput.amount = 100 * CENT; // full colored balance preserved
    transferOutput.script = recipientColoredScript;
    transferPstt.outputs.push_back(transferOutput);

    FlatSigningProvider coloredProvider;
    coloredProvider.keys[holderKey.GetPubKey().GetID()] = holderKey;
    coloredProvider.pubkeys[holderKey.GetPubKey().GetID()] = holderKey.GetPubKey();
    SignInputForTest(transferPstt, 0, coloredProvider);

    FlatSigningProvider feeProvider;
    feeProvider.keys[feeKey.GetPubKey().GetID()] = feeKey;
    feeProvider.pubkeys[feeKey.GetPubKey().GetID()] = feeKey.GetPubKey();
    SignInputForTest(transferPstt, 1, feeProvider);

    CheckMempoolResult(*this, MakeTransactionRef(ExtractPSTT(transferPstt)), true);
}

BOOST_FIXTURE_TEST_CASE(pstt_extract_invalid_token_without_fee, TestChainSetup)
{
    // Same colored issue as above, but the transfer has no separate TPC fee
    // input -- consensus rejects a colored transfer that pays no TPC fee.
    CTransactionRef p2pkhTx;
    CKey issuerKey;
    std::tie(p2pkhTx, issuerKey) = MakeConfirmedP2PKHCoin(*this, m_coinbase_txns[1]);

    ColorIdentifier colorId(p2pkhTx->vout[0].scriptPubKey);
    CKey holderKey;
    holderKey.MakeNewKey(true);
    CScript coloredScript = CScript() << colorId.toVector() << OP_COLOR
                                       << OP_DUP << OP_HASH160 << ToByteVector(holderKey.GetPubKey().GetID())
                                       << OP_EQUALVERIFY << OP_CHECKSIG;

    PartiallySignedTapyrusTransaction issuePstt;
    issuePstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput issueInput;
    issueInput.previous_txid = p2pkhTx->GetHashMalFix();
    issueInput.prev_out_index = 0;
    issueInput.previous_txid_set = true;
    issueInput.prev_out_index_set = true;
    issueInput.utxo = p2pkhTx;
    issuePstt.inputs.push_back(issueInput);

    PSTTOutput issueOutput;
    issueOutput.amount = 100 * CENT;
    issueOutput.script = coloredScript;
    issuePstt.outputs.push_back(issueOutput);

    FlatSigningProvider issueProvider;
    issueProvider.keys[issuerKey.GetPubKey().GetID()] = issuerKey;
    issueProvider.pubkeys[issuerKey.GetPubKey().GetID()] = issuerKey.GetPubKey();
    SignInputForTest(issuePstt, 0, issueProvider);

    CTransactionRef issueTx = MakeTransactionRef(ExtractPSTT(issuePstt));
    CheckMempoolResult(*this, issueTx, true);

    CKey recipientKey;
    recipientKey.MakeNewKey(true);
    CScript recipientColoredScript = CScript() << colorId.toVector() << OP_COLOR
                                                 << OP_DUP << OP_HASH160 << ToByteVector(recipientKey.GetPubKey().GetID())
                                                 << OP_EQUALVERIFY << OP_CHECKSIG;

    PartiallySignedTapyrusTransaction transferPstt;
    transferPstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput coloredInput;
    coloredInput.previous_txid = issueTx->GetHashMalFix();
    coloredInput.prev_out_index = 0;
    coloredInput.previous_txid_set = true;
    coloredInput.prev_out_index_set = true;
    coloredInput.utxo = issueTx;
    transferPstt.inputs.push_back(coloredInput);

    PSTTOutput transferOutput;
    transferOutput.amount = 100 * CENT;
    transferOutput.script = recipientColoredScript;
    transferPstt.outputs.push_back(transferOutput);

    FlatSigningProvider coloredProvider;
    coloredProvider.keys[holderKey.GetPubKey().GetID()] = holderKey;
    coloredProvider.pubkeys[holderKey.GetPubKey().GetID()] = holderKey.GetPubKey();
    SignInputForTest(transferPstt, 0, coloredProvider);

    CheckMempoolResult(*this, MakeTransactionRef(ExtractPSTT(transferPstt)), false, "bad-txns-token-without-fee");
}

// -----------------------------------------------------------------------
// Extractor
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_extractpstt_throws_on_missing_final_scriptsig)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    // No final_script_sig set on the one input -- not finalized yet.
    BOOST_CHECK_THROW(ExtractPSTT(pstt), std::runtime_error);
}

// -----------------------------------------------------------------------
// Combiner (Merge conflict policy)
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_merge_refuses_conflicting_partial_sig)
{
    PartiallySignedTapyrusTransaction a = MakeBasicPstt();
    PartiallySignedTapyrusTransaction b = a;

    CKey key;
    key.MakeNewKey(true);
    std::vector<unsigned char> sig1(71, 0x11);
    sig1.push_back(SIGHASH_ALL);
    std::vector<unsigned char> sig2(71, 0x22);
    sig2.push_back(SIGHASH_ALL);
    a.inputs[0].partial_sigs.emplace(key.GetPubKey().GetID(), SigPair(key.GetPubKey(), sig1));
    b.inputs[0].partial_sigs.emplace(key.GetPubKey().GetID(), SigPair(key.GetPubKey(), sig2));

    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pstt_merge_picks_first_on_sighash_type_conflict)
{
    // PSTT_IN_SIGHASH_TYPE is explicitly in the pick-first tier (not
    // refuse, not must-match): harmless duplication of a neutral field
    // should merge silently, keeping whichever side merges into.
    PartiallySignedTapyrusTransaction a = MakeBasicPstt();
    PartiallySignedTapyrusTransaction b = a;
    a.inputs[0].sighash_type = SIGHASH_ALL;
    b.inputs[0].sighash_type = SIGHASH_NONE;

    BOOST_CHECK_NO_THROW(a.Merge(b));
    BOOST_CHECK_EQUAL(*a.inputs[0].sighash_type, SIGHASH_ALL);
}

// -----------------------------------------------------------------------
// Shared fixture: funds a 2-of-2 P2SH multisig UTXO and returns two
// independently-signed (one key each), not-yet-merged spends of it. Reused
// by both the library-level Combine/Finalize/Extract test below and the
// RPC-dispatch-level tests further down.
// -----------------------------------------------------------------------

struct MultisigSpendPair
{
    CScript redeemScript;
    CTransactionRef multisigUtxo;
    PartiallySignedTapyrusTransaction signedByA; // one of two required signatures
    PartiallySignedTapyrusTransaction signedByB; // the other
};

static MultisigSpendPair MakeUnmergedMultisigSpendPair(TestChainSetup& setup)
{
    CKey keyA, keyB;
    keyA.MakeNewKey(true);
    keyB.MakeNewKey(true);
    CScript redeemScript = GetScriptForMultisig(2, {keyA.GetPubKey(), keyB.GetPubKey()});
    CScript scriptPubKey = GetScriptForDestination(CScriptID(redeemScript));

    const CTransactionRef& coinbaseTx = setup.m_coinbase_txns[0];
    PartiallySignedTapyrusTransaction fundingPstt;
    fundingPstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput fundingInput;
    fundingInput.previous_txid = coinbaseTx->GetHashMalFix();
    fundingInput.prev_out_index = 0;
    fundingInput.previous_txid_set = true;
    fundingInput.prev_out_index_set = true;
    fundingInput.utxo = coinbaseTx;
    fundingPstt.inputs.push_back(fundingInput);
    PSTTOutput fundingOutput;
    fundingOutput.amount = coinbaseTx->vout[0].nValue - CENT;
    fundingOutput.script = scriptPubKey;
    fundingPstt.outputs.push_back(fundingOutput);

    FlatSigningProvider fundingProvider;
    fundingProvider.keys[setup.coinbaseKey.GetPubKey().GetID()] = setup.coinbaseKey;
    SignInputForTest(fundingPstt, 0, fundingProvider);
    CTransactionRef multisigUtxo = MakeTransactionRef(ExtractPSTT(fundingPstt));
    CheckMempoolResult(setup, multisigUtxo, true);

    // Two independent parties each build the same spend and sign it with
    // their own key only.
    CKey destKey;
    destKey.MakeNewKey(true);
    auto makeSpend = [&]() {
        PartiallySignedTapyrusTransaction pstt;
        pstt.tx_features = CTransaction::CURRENT_FEATURES;
        PSTTInput input;
        input.previous_txid = multisigUtxo->GetHashMalFix();
        input.prev_out_index = 0;
        input.previous_txid_set = true;
        input.prev_out_index_set = true;
        input.utxo = multisigUtxo;
        input.redeem_script = redeemScript;
        pstt.inputs.push_back(input);
        PSTTOutput output;
        output.amount = multisigUtxo->vout[0].nValue - CENT;
        output.script = P2PKHScriptFor(destKey);
        pstt.outputs.push_back(output);
        return pstt;
    };

    PartiallySignedTapyrusTransaction signedByA = makeSpend();
    FlatSigningProvider providerA;
    providerA.keys[keyA.GetPubKey().GetID()] = keyA;
    SignatureData sigdataA;
    BOOST_REQUIRE(SignPSTTInput(providerA, signedByA, 0, sigdataA, SIGHASH_ALL, SignatureScheme::ECDSA) == PSTTSignResult::OK);
    signedByA.inputs[0].FromSignatureData(sigdataA);
    BOOST_REQUIRE(!sigdataA.complete);

    PartiallySignedTapyrusTransaction signedByB = makeSpend();
    FlatSigningProvider providerB;
    providerB.keys[keyB.GetPubKey().GetID()] = keyB;
    SignatureData sigdataB;
    BOOST_REQUIRE(SignPSTTInput(providerB, signedByB, 0, sigdataB, SIGHASH_ALL, SignatureScheme::ECDSA) == PSTTSignResult::OK);
    signedByB.inputs[0].FromSignatureData(sigdataB);
    BOOST_REQUIRE(!sigdataB.complete);

    return {redeemScript, multisigUtxo, signedByA, signedByB};
}

// -----------------------------------------------------------------------
// End-to-end Combine -> Finalize -> Extract, mirroring the finalizepstt RPC's
// completeness detection (SignPSTTInput against an empty/dummy provider)
// without depending on the RPC layer itself.
// -----------------------------------------------------------------------

BOOST_FIXTURE_TEST_CASE(pstt_combine_finalize_extract_p2sh_multisig, TestChainSetup)
{
    MultisigSpendPair fx = MakeUnmergedMultisigSpendPair(*this);
    PartiallySignedTapyrusTransaction merged = fx.signedByA;

    // Combiner: merge the two independently-signed copies.
    BOOST_REQUIRE(merged.HasSameIdentifierAs(fx.signedByB));
    merged.Merge(fx.signedByB);
    BOOST_CHECK(merged.IsSane());
    BOOST_CHECK_EQUAL(merged.inputs[0].partial_sigs.size(), 2U);

    // Finalizer: mirrors finalizepstt's DUMMY_SIGNING_PROVIDER completeness
    // check -- an empty provider can't add new signatures, only combine the
    // two partial ones already present into a complete scriptSig.
    SignatureData finalSigdata;
    BOOST_REQUIRE(SignPSTTInput(DUMMY_SIGNING_PROVIDER, merged, 0, finalSigdata, SIGHASH_ALL, SignatureScheme::ECDSA) == PSTTSignResult::OK);
    BOOST_CHECK(finalSigdata.complete);
    merged.inputs[0].FromSignatureData(finalSigdata);
    BOOST_CHECK(!merged.inputs[0].final_script_sig.empty());

    // Extractor: produces a sendrawtransaction-acceptable transaction.
    CheckMempoolResult(*this, MakeTransactionRef(ExtractPSTT(merged)), true);
}

// -----------------------------------------------------------------------
// RPC-dispatch-level tests. The tests above exercise SignPSTTInput/Merge/
// ExtractPSTT directly; these instead go through the real registered
// combinepstt/finalizepstt/extractpstt actors (tableRPC), so argument
// parsing, base64/JSON encoding, and error wrapping are covered too, not
// just the pstt.cpp library calls underneath them.
// -----------------------------------------------------------------------

static UniValue CallPsttRPC(const std::string& method, const UniValue& params)
{
    BOOST_REQUIRE(tableRPC[method]);
    JSONRPCRequest request;
    request.strMethod = method;
    request.params = params;
    request.fHelp = false;
    rpcfn_type fn = tableRPC[method]->actor;
    return (*fn)(request);
}

static CMutableTransaction DecodeHexTxForTest(const std::string& hex)
{
    CDataStream ss(ParseHex(hex), SER_NETWORK, PROTOCOL_VERSION);
    CMutableTransaction mtx;
    ss >> mtx;
    return mtx;
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_combine_finalize_extract_roundtrip, TestChainSetup)
{
    MultisigSpendPair fx = MakeUnmergedMultisigSpendPair(*this);

    UniValue txs(UniValue::VARR);
    txs.push_back(EncodePSTT(fx.signedByA));
    txs.push_back(EncodePSTT(fx.signedByB));
    UniValue combineParams(UniValue::VARR);
    combineParams.push_back(txs);
    UniValue combined = CallPsttRPC("combinepstt", combineParams);
    BOOST_REQUIRE(combined.isStr());

    PartiallySignedTapyrusTransaction mergedPstt;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(mergedPstt, combined.get_str(), decodeError));
    BOOST_CHECK_EQUAL(mergedPstt.inputs[0].partial_sigs.size(), 2U);

    UniValue finalizeParams(UniValue::VARR);
    finalizeParams.push_back(combined.get_str());
    finalizeParams.push_back(UniValue(true));
    UniValue finalizeResult = CallPsttRPC("finalizepstt", finalizeParams);
    BOOST_REQUIRE(finalizeResult.isObject());
    BOOST_CHECK(finalizeResult.find_value("complete").get_bool());

    CTransactionRef finalTx = MakeTransactionRef(DecodeHexTxForTest(finalizeResult.find_value("hex").get_str()));
    CheckMempoolResult(*this, finalTx, true);
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_finalizepstt_no_extract_then_extractpstt, TestChainSetup)
{
    MultisigSpendPair fx = MakeUnmergedMultisigSpendPair(*this);

    UniValue txs(UniValue::VARR);
    txs.push_back(EncodePSTT(fx.signedByA));
    txs.push_back(EncodePSTT(fx.signedByB));
    UniValue combineParams(UniValue::VARR);
    combineParams.push_back(txs);
    UniValue combined = CallPsttRPC("combinepstt", combineParams);

    UniValue finalizeParams(UniValue::VARR);
    finalizeParams.push_back(combined.get_str());
    finalizeParams.push_back(UniValue(false)); // extract=false -> expect a "pstt" field back, not "hex"
    UniValue finalizeResult = CallPsttRPC("finalizepstt", finalizeParams);
    BOOST_CHECK(finalizeResult.find_value("complete").get_bool());
    BOOST_CHECK(finalizeResult.find_value("hex").isNull());
    std::string finalizedPsttB64 = finalizeResult.find_value("pstt").get_str();

    UniValue extractParams(UniValue::VARR);
    extractParams.push_back(finalizedPsttB64);
    UniValue extracted = CallPsttRPC("extractpstt", extractParams);
    BOOST_REQUIRE(extracted.isStr());

    CTransactionRef finalTx = MakeTransactionRef(DecodeHexTxForTest(extracted.get_str()));
    CheckMempoolResult(*this, finalTx, true);
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_combinepstt_rejects_different_identifiers, TestingSetup)
{
    UniValue txs(UniValue::VARR);
    txs.push_back(EncodePSTT(MakeBasicPstt()));
    txs.push_back(EncodePSTT(MakeBasicPstt())); // fresh random utxo/scripts -> different identifier
    UniValue combineParams(UniValue::VARR);
    combineParams.push_back(txs);

    BOOST_CHECK_THROW(CallPsttRPC("combinepstt", combineParams), UniValue);
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_finalizepstt_refuses_while_modifiable, TestingSetup)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.tx_modifiable = PSTT_TXMOD_INPUTS_MODIFIABLE;

    UniValue finalizeParams(UniValue::VARR);
    finalizeParams.push_back(EncodePSTT(pstt));

    BOOST_CHECK_THROW(CallPsttRPC("finalizepstt", finalizeParams), UniValue);
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_extractpstt_throws_when_incomplete, TestingSetup)
{
    // No final_script_sig on the one input -- not finalized yet.
    UniValue extractParams(UniValue::VARR);
    extractParams.push_back(EncodePSTT(MakeBasicPstt()));

    BOOST_CHECK_THROW(CallPsttRPC("extractpstt", extractParams), UniValue);
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_signpsttwithkey_p2pk_coinbase_spend, TestChainSetup)
{
    // Same shape as pstt_extract_valid_p2pk_coinbase_spend, but signed
    // through the real registered signpsttwithkey actor (tableRPC) instead
    // of calling SignPSTTInput directly -- covers WIF-string parsing,
    // sighash/sigscheme string parsing, and the pstt/complete JSON result.
    const CTransactionRef& prevTx = m_coinbase_txns[0];
    CKey destKey;
    destKey.MakeNewKey(true);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput input;
    input.previous_txid = prevTx->GetHashMalFix();
    input.prev_out_index = 0;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    input.utxo = prevTx;
    pstt.inputs.push_back(input);

    PSTTOutput output;
    output.amount = prevTx->vout[0].nValue - CENT;
    output.script = P2PKHScriptFor(destKey);
    pstt.outputs.push_back(output);

    UniValue keys(UniValue::VARR);
    keys.push_back(EncodeSecret(coinbaseKey));

    UniValue signParams(UniValue::VARR);
    signParams.push_back(EncodePSTT(pstt));
    signParams.push_back(keys);
    UniValue signResult = CallPsttRPC("signpsttwithkey", signParams);
    BOOST_REQUIRE(signResult.isObject());
    BOOST_CHECK(signResult.find_value("complete").get_bool());

    PartiallySignedTapyrusTransaction signedPstt;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(signedPstt, signResult.find_value("pstt").get_str(), decodeError));
    BOOST_CHECK(!signedPstt.inputs[0].final_script_sig.empty());

    CheckMempoolResult(*this, MakeTransactionRef(ExtractPSTT(signedPstt)), true);
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_signpsttwithkey_rejects_invalid_sigscheme, TestingSetup)
{
    // Confirms the RPC handler itself (not just ParseSigSchemeString in
    // isolation) rejects an unrecognized sigscheme string rather than
    // silently falling back to ECDSA. CallPsttRPC invokes the registered
    // actor function pointer directly, bypassing the JSON-RPC dispatch
    // wrapper (rpc/server.cpp) that would otherwise convert a bare
    // std::exception into a UniValue JSONRPCError for a real client -- so
    // here the raw std::runtime_error ParseSigSchemeString throws is what
    // surfaces, unlike the JSONRPCError-based throws elsewhere in this file.
    UniValue keys(UniValue::VARR);

    UniValue signParams(UniValue::VARR);
    signParams.push_back(EncodePSTT(MakeBasicPstt()));
    signParams.push_back(keys);
    signParams.push_back(UniValue("ALL"));
    signParams.push_back(UniValue("Schnorr")); // wrong case -- must not silently mean SCHNORR or ECDSA

    BOOST_CHECK_THROW(CallPsttRPC("signpsttwithkey", signParams), std::runtime_error);
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_finalizepstt_strips_signing_only_fields, TestChainSetup)
{
    // finalizepstt's field-strip policy (rawtransaction.cpp) is meant to
    // clear everything only useful during signing once an input is
    // finalized. PSTTInput::FromSignatureData already clears partial_sigs/
    // hd_keypaths/redeem_script itself on completion, so to actually
    // exercise finalizepstt's own explicit strip statements this attaches
    // fields FromSignatureData never touches (sighash_type, a preimage).
    MultisigSpendPair fx = MakeUnmergedMultisigSpendPair(*this);
    PartiallySignedTapyrusTransaction merged = fx.signedByA;
    merged.Merge(fx.signedByB);
    BOOST_REQUIRE_EQUAL(merged.inputs[0].partial_sigs.size(), 2U);
    BOOST_REQUIRE(merged.inputs[0].final_script_sig.empty());

    merged.inputs[0].sighash_type = SIGHASH_ALL;
    merged.inputs[0].ripemd160_preimages[std::vector<unsigned char>(20, 0xAB)] = std::vector<unsigned char>{0x01, 0x02};

    UniValue finalizeParams(UniValue::VARR);
    finalizeParams.push_back(EncodePSTT(merged));
    finalizeParams.push_back(UniValue(false)); // extract=false -- inspect the PSTT, not the extracted tx
    UniValue finalizeResult = CallPsttRPC("finalizepstt", finalizeParams);
    BOOST_REQUIRE(finalizeResult.find_value("complete").get_bool());

    PartiallySignedTapyrusTransaction finalized;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(finalized, finalizeResult.find_value("pstt").get_str(), decodeError));

    const PSTTInput& in = finalized.inputs[0];
    BOOST_CHECK(!in.final_script_sig.empty());
    BOOST_CHECK(in.partial_sigs.empty());
    BOOST_CHECK(!in.sighash_type);
    BOOST_CHECK(in.redeem_script.empty());
    BOOST_CHECK(in.hd_keypaths.empty());
    BOOST_CHECK(in.ripemd160_preimages.empty());
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_finalizepstt_partial_completion_mixed_missing_utxo, TestChainSetup)
{
    // Input 0 has both required multisig partial sigs already (finalizepstt
    // can complete it). Input 1 has a previous_txid/prev_out_index but no
    // attached UTXO at all -- as if a co-signer's Constructor/Updater step
    // hasn't happened yet. finalizepstt must finalize what it can and
    // report complete=false for the whole PSTT, not throw.
    //
    // Both inputs must be part of the transaction shape from the moment
    // each cosigner signs -- SIGHASH_ALL covers every input's prevout, so
    // appending input 1 only after input 0 was already signed (as a plain
    // reuse of MakeUnmergedMultisigSpendPair's 1-input fixture would do)
    // changes the signed tx shape and invalidates both partial sigs.
    CKey keyA, keyB;
    keyA.MakeNewKey(true);
    keyB.MakeNewKey(true);
    CScript redeemScript = GetScriptForMultisig(2, {keyA.GetPubKey(), keyB.GetPubKey()});
    CScript scriptPubKey = GetScriptForDestination(CScriptID(redeemScript));

    const CTransactionRef& coinbaseTx = m_coinbase_txns[0];
    PartiallySignedTapyrusTransaction fundingPstt;
    fundingPstt.tx_features = CTransaction::CURRENT_FEATURES;
    PSTTInput fundingInput;
    fundingInput.previous_txid = coinbaseTx->GetHashMalFix();
    fundingInput.prev_out_index = 0;
    fundingInput.previous_txid_set = true;
    fundingInput.prev_out_index_set = true;
    fundingInput.utxo = coinbaseTx;
    fundingPstt.inputs.push_back(fundingInput);
    PSTTOutput fundingOutput;
    fundingOutput.amount = coinbaseTx->vout[0].nValue - CENT;
    fundingOutput.script = scriptPubKey;
    fundingPstt.outputs.push_back(fundingOutput);
    FlatSigningProvider fundingProvider;
    fundingProvider.keys[coinbaseKey.GetPubKey().GetID()] = coinbaseKey;
    SignInputForTest(fundingPstt, 0, fundingProvider);
    CTransactionRef multisigUtxo = MakeTransactionRef(ExtractPSTT(fundingPstt));
    CheckMempoolResult(*this, multisigUtxo, true);

    CKey destKey;
    destKey.MakeNewKey(true);
    uint256 missingUtxoTxid = GetRandHash(); // same placeholder on both sides, so Merge() sees matching inputs

    auto makeSpend = [&]() {
        PartiallySignedTapyrusTransaction pstt;
        pstt.tx_features = CTransaction::CURRENT_FEATURES;

        PSTTInput multisigInput;
        multisigInput.previous_txid = multisigUtxo->GetHashMalFix();
        multisigInput.prev_out_index = 0;
        multisigInput.previous_txid_set = true;
        multisigInput.prev_out_index_set = true;
        multisigInput.utxo = multisigUtxo;
        multisigInput.redeem_script = redeemScript;
        pstt.inputs.push_back(multisigInput);

        PSTTInput missingUtxoInput;
        missingUtxoInput.previous_txid = missingUtxoTxid;
        missingUtxoInput.prev_out_index = 0;
        missingUtxoInput.previous_txid_set = true;
        missingUtxoInput.prev_out_index_set = true;
        pstt.inputs.push_back(missingUtxoInput);

        PSTTOutput output;
        output.amount = multisigUtxo->vout[0].nValue - CENT;
        output.script = P2PKHScriptFor(destKey);
        pstt.outputs.push_back(output);
        return pstt;
    };

    PartiallySignedTapyrusTransaction signedByA = makeSpend();
    FlatSigningProvider providerA;
    providerA.keys[keyA.GetPubKey().GetID()] = keyA;
    SignatureData sigdataA;
    BOOST_REQUIRE(SignPSTTInput(providerA, signedByA, 0, sigdataA, SIGHASH_ALL, SignatureScheme::ECDSA) == PSTTSignResult::OK);
    signedByA.inputs[0].FromSignatureData(sigdataA);
    BOOST_REQUIRE(!sigdataA.complete);

    PartiallySignedTapyrusTransaction signedByB = makeSpend();
    FlatSigningProvider providerB;
    providerB.keys[keyB.GetPubKey().GetID()] = keyB;
    SignatureData sigdataB;
    BOOST_REQUIRE(SignPSTTInput(providerB, signedByB, 0, sigdataB, SIGHASH_ALL, SignatureScheme::ECDSA) == PSTTSignResult::OK);
    signedByB.inputs[0].FromSignatureData(sigdataB);
    BOOST_REQUIRE(!sigdataB.complete);

    PartiallySignedTapyrusTransaction pstt = signedByA;
    pstt.Merge(signedByB);
    BOOST_REQUIRE_EQUAL(pstt.inputs[0].partial_sigs.size(), 2U);

    UniValue finalizeParams(UniValue::VARR);
    finalizeParams.push_back(EncodePSTT(pstt));
    finalizeParams.push_back(UniValue(false));
    UniValue finalizeResult = CallPsttRPC("finalizepstt", finalizeParams);
    BOOST_CHECK(!finalizeResult.find_value("complete").get_bool());
    BOOST_CHECK(finalizeResult.find_value("hex").isNull());

    PartiallySignedTapyrusTransaction result;
    std::string decodeError;
    BOOST_REQUIRE(DecodePSTT(result, finalizeResult.find_value("pstt").get_str(), decodeError));
    BOOST_CHECK(!result.inputs[0].final_script_sig.empty()); // completable input got finalized
    BOOST_CHECK(result.inputs[1].final_script_sig.empty());  // MISSING_UTXO input left alone
}

BOOST_FIXTURE_TEST_CASE(pstt_rpc_decodepstt_shape, TestChainSetup)
{
    // Asserts decodepstt's JSON shape (top-level keys plus per-input/
    // per-output nested shapes), not just that it doesn't throw.
    MultisigSpendPair fx = MakeUnmergedMultisigSpendPair(*this);
    PartiallySignedTapyrusTransaction pstt = fx.signedByA;
    pstt.tx_modifiable = PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE;

    UniValue params(UniValue::VARR);
    params.push_back(EncodePSTT(pstt));
    UniValue decoded = CallPsttRPC("decodepstt", params);

    BOOST_REQUIRE(decoded.isObject());
    BOOST_CHECK(decoded.exists("tx_features"));
    BOOST_REQUIRE(decoded.exists("tx_modifiable"));
    const UniValue& mod = decoded.find_value("tx_modifiable");
    BOOST_REQUIRE(mod.isObject());
    BOOST_CHECK(mod.find_value("inputs_modifiable").get_bool());
    BOOST_CHECK(mod.find_value("outputs_modifiable").get_bool());
    BOOST_CHECK(!mod.find_value("has_sighash_single").get_bool());
    BOOST_CHECK(decoded.exists("identification_txid"));

    BOOST_REQUIRE(decoded.exists("inputs"));
    const UniValue& inputs = decoded.find_value("inputs");
    BOOST_REQUIRE(inputs.isArray());
    BOOST_REQUIRE_EQUAL(inputs.size(), 1U);
    const UniValue& in0 = inputs[0];
    BOOST_CHECK(in0.exists("previous_txid"));
    BOOST_CHECK(in0.exists("output_index"));
    BOOST_CHECK(in0.exists("utxo"));
    BOOST_REQUIRE(in0.exists("partial_signatures"));
    BOOST_CHECK(in0.find_value("partial_signatures").isObject());
    BOOST_CHECK(in0.exists("redeem_script"));
    BOOST_CHECK(!in0.exists("final_scriptSig")); // not finalized yet

    BOOST_REQUIRE(decoded.exists("outputs"));
    const UniValue& outputs = decoded.find_value("outputs");
    BOOST_REQUIRE(outputs.isArray());
    BOOST_REQUIRE_EQUAL(outputs.size(), 1U);
    const UniValue& out0 = outputs[0];
    BOOST_CHECK(out0.exists("amount_raw"));
    BOOST_CHECK(out0.exists("script"));
}

// -----------------------------------------------------------------------
// PSTTInput::Merge() / PSTTOutput::Merge() conflict and mismatch throw sites
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_merge_refuses_mismatched_input_output_counts)
{
    PartiallySignedTapyrusTransaction a = MakeBasicPstt();
    PartiallySignedTapyrusTransaction b = a;
    CTransactionRef utxo2 = MakeSimpleUtxoTx(RandomP2PKHScript(), 5000);
    b.inputs.push_back(MakeBasicInput(utxo2->GetHashMalFix(), 0, utxo2));
    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pstt_input_merge_refuses_conflicting_utxo)
{
    CTransactionRef utxo1 = MakeSimpleUtxoTx(RandomP2PKHScript(), 1000);
    CTransactionRef utxo2 = MakeSimpleUtxoTx(RandomP2PKHScript(), 2000);
    PSTTInput a = MakeBasicInput(utxo1->GetHashMalFix(), 0, utxo1);
    PSTTInput b = a;
    b.utxo = utxo2; // different tx attached to the "same" input
    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pstt_input_merge_refuses_conflicting_final_scriptsig)
{
    CTransactionRef utxo = MakeSimpleUtxoTx(RandomP2PKHScript(), 1000);
    PSTTInput a = MakeBasicInput(utxo->GetHashMalFix(), 0, utxo);
    PSTTInput b = a;
    a.final_script_sig = CScript() << OP_TRUE;
    b.final_script_sig = CScript() << OP_FALSE;
    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pstt_input_merge_refuses_conflicting_partial_sig)
{
    CTransactionRef utxo = MakeSimpleUtxoTx(RandomP2PKHScript(), 1000);
    PSTTInput a = MakeBasicInput(utxo->GetHashMalFix(), 0, utxo);
    PSTTInput b = a;
    CKey key;
    key.MakeNewKey(true);
    std::vector<unsigned char> sig1(71, 0x11);
    sig1.push_back(1);
    std::vector<unsigned char> sig2(71, 0x22);
    sig2.push_back(1);
    a.partial_sigs.emplace(key.GetPubKey().GetID(), SigPair(key.GetPubKey(), sig1));
    b.partial_sigs.emplace(key.GetPubKey().GetID(), SigPair(key.GetPubKey(), sig2));
    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pstt_input_merge_refuses_mismatched_redeem_script)
{
    CTransactionRef utxo = MakeSimpleUtxoTx(RandomP2PKHScript(), 1000);
    PSTTInput a = MakeBasicInput(utxo->GetHashMalFix(), 0, utxo);
    PSTTInput b = a;
    a.redeem_script = CScript() << OP_TRUE;
    b.redeem_script = CScript() << OP_FALSE;
    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pstt_input_merge_refuses_mismatched_bip32_derivation)
{
    CTransactionRef utxo = MakeSimpleUtxoTx(RandomP2PKHScript(), 1000);
    PSTTInput a = MakeBasicInput(utxo->GetHashMalFix(), 0, utxo);
    PSTTInput b = a;
    CKey key;
    key.MakeNewKey(true);
    a.hd_keypaths.emplace(key.GetPubKey(), std::vector<uint32_t>{1, 2, 3});
    b.hd_keypaths.emplace(key.GetPubKey(), std::vector<uint32_t>{4, 5, 6});
    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pstt_output_merge_refuses_mismatched_redeem_script)
{
    PSTTOutput a;
    a.amount = 1000;
    a.script = RandomP2PKHScript();
    PSTTOutput b = a;
    a.redeem_script = CScript() << OP_TRUE;
    b.redeem_script = CScript() << OP_FALSE;
    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(pstt_output_merge_refuses_mismatched_bip32_derivation)
{
    PSTTOutput a;
    a.amount = 1000;
    a.script = RandomP2PKHScript();
    PSTTOutput b = a;
    CKey key;
    key.MakeNewKey(true);
    a.hd_keypaths.emplace(key.GetPubKey(), std::vector<uint32_t>{1});
    b.hd_keypaths.emplace(key.GetPubKey(), std::vector<uint32_t>{2});
    BOOST_CHECK_THROW(a.Merge(b), std::invalid_argument);
}

// -----------------------------------------------------------------------
// Output-side reserved keytype rejection (analog to
// pstt_rejects_reserved_input_keytype above, for PSTTOutput)
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_rejects_reserved_output_keytype)
{
    for (uint8_t reserved : {0x01, 0x05, 0x06, 0x07}) {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        SerializeToVector(ss, reserved);
        ss << std::vector<unsigned char>{0x00};
        ss << PSTT_SEPARATOR;

        std::vector<unsigned char> data(ss.begin(), ss.end());
        BOOST_CHECK_THROW(UnserializeObj<PSTTOutput>(data), std::ios_base::failure);
    }
}

// -----------------------------------------------------------------------
// PSTT_IN_REQUIRED_*_LOCKTIME parse-time range validation
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_rejects_time_locktime_too_low)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    SerializeToVector(ss, PSTT_IN_REQUIRED_TIME_LOCKTIME);
    SerializeToVector(ss, (uint32_t)499999999);
    ss << PSTT_SEPARATOR;

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PSTTInput>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_rejects_height_locktime_zero)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    SerializeToVector(ss, PSTT_IN_REQUIRED_HEIGHT_LOCKTIME);
    SerializeToVector(ss, (uint32_t)0);
    ss << PSTT_SEPARATOR;

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PSTTInput>(data), std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(pstt_rejects_height_locktime_too_high)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    SerializeToVector(ss, PSTT_IN_REQUIRED_HEIGHT_LOCKTIME);
    SerializeToVector(ss, (uint32_t)500000000);
    ss << PSTT_SEPARATOR;

    std::vector<unsigned char> data(ss.begin(), ss.end());
    BOOST_CHECK_THROW(UnserializeObj<PSTTInput>(data), std::ios_base::failure);
}

// -----------------------------------------------------------------------
// PSTT_GLOBAL_TX_MODIFIABLE reserved bits (3-7)
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_issane_rejects_reserved_txmod_bits)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.tx_modifiable = 1 << 3; // lowest reserved bit
    BOOST_CHECK(!pstt.IsSane());
}

// -----------------------------------------------------------------------
// SignPSTTInput() non-OK result coverage. These call SignPSTTInput()
// directly on hand-built PSTTs rather than going through the mempool-level
// pipeline above -- each case is set up to fail at one specific check, so
// DUMMY_SIGNING_PROVIDER (no real keys) is sufficient; none of these reach
// ProduceSignature().
// -----------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(pstt_sign_missing_utxo)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.inputs[0].utxo = nullptr;
    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, 0, sigdata, SIGHASH_ALL);
    BOOST_CHECK(result == PSTTSignResult::MISSING_UTXO);
}

BOOST_AUTO_TEST_CASE(pstt_sign_utxo_txid_mismatch)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    // Swap in an unrelated UTXO whose hash no longer matches previous_txid.
    pstt.inputs[0].utxo = MakeSimpleUtxoTx(RandomP2PKHScript(), 5000);
    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, 0, sigdata, SIGHASH_ALL);
    BOOST_CHECK(result == PSTTSignResult::UTXO_TXID_MISMATCH);
}

BOOST_AUTO_TEST_CASE(pstt_sign_prev_out_index_oob)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.inputs[0].prev_out_index = 5; // utxo only has a single output (index 0)
    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, 0, sigdata, SIGHASH_ALL);
    BOOST_CHECK(result == PSTTSignResult::PREV_OUT_INDEX_OOB);
}

BOOST_AUTO_TEST_CASE(pstt_sign_redeem_script_hash_mismatch)
{
    CScript actualRedeem = CScript() << OP_TRUE;
    CScript scriptPubKey = CScript() << OP_HASH160 << ToByteVector(CScriptID(actualRedeem)) << OP_EQUAL;
    CTransactionRef utxo = MakeSimpleUtxoTx(scriptPubKey, 100000);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    pstt.inputs.push_back(MakeBasicInput(utxo->GetHashMalFix(), 0, utxo));
    PSTTOutput output;
    output.amount = 90000;
    output.script = RandomP2PKHScript();
    pstt.outputs.push_back(output);

    pstt.inputs[0].redeem_script = CScript() << OP_FALSE; // wrong script -- different hash

    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, 0, sigdata, SIGHASH_ALL);
    BOOST_CHECK(result == PSTTSignResult::REDEEM_SCRIPT_HASH_MISMATCH);
}

BOOST_AUTO_TEST_CASE(pstt_sign_sighash_conflict)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    pstt.inputs[0].sighash_type = SIGHASH_ALL;
    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, 0, sigdata, SIGHASH_NONE);
    BOOST_CHECK(result == PSTTSignResult::SIGHASH_CONFLICT);
}

BOOST_AUTO_TEST_CASE(pstt_sign_sighash_single_oob)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt(); // 1 input, 1 output
    CTransactionRef utxo2 = MakeSimpleUtxoTx(RandomP2PKHScript(), 50000);
    pstt.inputs.push_back(MakeBasicInput(utxo2->GetHashMalFix(), 0, utxo2)); // 2nd input, no matching 2nd output
    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, 1, sigdata, SIGHASH_SINGLE);
    BOOST_CHECK(result == PSTTSignResult::SIGHASH_SINGLE_OOB);
}

BOOST_AUTO_TEST_CASE(pstt_sign_locktime_invalid)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    CTransactionRef utxo2 = MakeSimpleUtxoTx(RandomP2PKHScript(), 50000);
    pstt.inputs.push_back(MakeBasicInput(utxo2->GetHashMalFix(), 0, utxo2));
    // Input 0 only accepts height, input 1 only accepts time -> empty intersection.
    pstt.inputs[0].required_height_locktime = 800;
    pstt.inputs[1].required_time_locktime = 700000000;
    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, 0, sigdata, SIGHASH_ALL);
    BOOST_CHECK(result == PSTTSignResult::LOCKTIME_INVALID);
}

BOOST_AUTO_TEST_CASE(pstt_sign_scheme_conflict)
{
    PartiallySignedTapyrusTransaction pstt = MakeBasicPstt();
    CKey key1;
    key1.MakeNewKey(true);
    std::vector<unsigned char> schnorr_sig(CPubKey::COMPACT_SIGNATURE_SIZE, 0x22); // already-present Schnorr sig
    pstt.inputs[0].partial_sigs.emplace(key1.GetPubKey().GetID(), SigPair(key1.GetPubKey(), schnorr_sig));

    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, 0, sigdata, SIGHASH_ALL, SignatureScheme::ECDSA);
    BOOST_CHECK(result == PSTTSignResult::SCHEME_CONFLICT);
}

BOOST_AUTO_TEST_SUITE_END()
