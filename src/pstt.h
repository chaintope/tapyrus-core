// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Partially Signed Tapyrus Transaction (PSTT), per TIP-174
// (chaintope/tips, tip-0174.md). Tapyrus-native replacement for the
// Bitcoin-shaped PSBT (BIP-174 "v0") format previously carried in
// script/sign.h/.cpp -- no global unsigned tx, per-input outpoint fields
// instead, Colored Coin aware, ECDSA/Schnorr aware.

#ifndef TAPYRUS_PSTT_H
#define TAPYRUS_PSTT_H

#include <boost/optional.hpp>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/sign.h>
#include <uint256.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------
// Magic bytes
// ---------------------------------------------------------------------

static constexpr uint8_t PSTT_MAGIC_BYTES[5] = {'p', 's', 't', 't', 0xFF};

// ---------------------------------------------------------------------
// Global types
// ---------------------------------------------------------------------

static constexpr uint8_t PSTT_GLOBAL_XPUB              = 0x01;
static constexpr uint8_t PSTT_GLOBAL_TX_FEATURES        = 0x02; // required
static constexpr uint8_t PSTT_GLOBAL_FALLBACK_LOCKTIME  = 0x03;
static constexpr uint8_t PSTT_GLOBAL_INPUT_COUNT        = 0x04; // required
static constexpr uint8_t PSTT_GLOBAL_OUTPUT_COUNT       = 0x05; // required
static constexpr uint8_t PSTT_GLOBAL_TX_MODIFIABLE      = 0x06;
static constexpr uint8_t PSTT_GLOBAL_VERSION            = 0xFB;
static constexpr uint8_t PSTT_GLOBAL_PROPRIETARY        = 0xFC;
// 0x00 is reserved (BIP-174's global unsigned tx, no counterpart here) --
// must be actively rejected, not silently folded into unknown.

// ---------------------------------------------------------------------
// Input types
// ---------------------------------------------------------------------

static constexpr uint8_t PSTT_IN_UTXO                     = 0x00; // required before signing
static constexpr uint8_t PSTT_IN_PARTIAL_SIG               = 0x02;
static constexpr uint8_t PSTT_IN_SIGHASH_TYPE              = 0x03;
static constexpr uint8_t PSTT_IN_REDEEM_SCRIPT             = 0x04;
static constexpr uint8_t PSTT_IN_BIP32_DERIVATION          = 0x06;
static constexpr uint8_t PSTT_IN_FINAL_SCRIPTSIG           = 0x07;
static constexpr uint8_t PSTT_IN_RIPEMD160                 = 0x0a;
static constexpr uint8_t PSTT_IN_SHA256                    = 0x0b;
static constexpr uint8_t PSTT_IN_HASH160                   = 0x0c;
static constexpr uint8_t PSTT_IN_HASH256                   = 0x0d;
static constexpr uint8_t PSTT_IN_PREVIOUS_TXID             = 0x0e; // required
static constexpr uint8_t PSTT_IN_OUTPUT_INDEX              = 0x0f; // required
static constexpr uint8_t PSTT_IN_SEQUENCE                  = 0x10;
static constexpr uint8_t PSTT_IN_REQUIRED_TIME_LOCKTIME    = 0x11;
static constexpr uint8_t PSTT_IN_REQUIRED_HEIGHT_LOCKTIME  = 0x12;
static constexpr uint8_t PSTT_IN_PROPRIETARY               = 0xFC;
// Reserved/must-reject on inputs: 0x01 (witness UTXO), 0x05 (witness script),
// 0x08 (finalized scriptWitness), 0x09 (proof-of-reserves commitment), and
// the Taproot-related type values from BIP-371 (PSBT_IN_TAP_KEY_SIG=0x13
// through PSBT_IN_TAP_MERKLE_ROOT=0x18) -- none have a Tapyrus counterpart.

// ---------------------------------------------------------------------
// Output types
// ---------------------------------------------------------------------

static constexpr uint8_t PSTT_OUT_REDEEM_SCRIPT    = 0x00;
static constexpr uint8_t PSTT_OUT_BIP32_DERIVATION  = 0x02;
static constexpr uint8_t PSTT_OUT_AMOUNT             = 0x03; // required
static constexpr uint8_t PSTT_OUT_SCRIPT              = 0x04; // required
static constexpr uint8_t PSTT_OUT_PROPRIETARY          = 0xFC;
// Reserved/must-reject on outputs: 0x01 (witness script), and the
// Taproot-related type values from BIP-371 (PSBT_OUT_TAP_INTERNAL_KEY=0x05
// through PSBT_OUT_TAP_BIP32_DERIVATION=0x07).

static constexpr uint8_t PSTT_SEPARATOR = 0x00;

// PSTT_GLOBAL_TX_MODIFIABLE bitfield
static constexpr uint8_t PSTT_TXMOD_INPUTS_MODIFIABLE  = 1 << 0;
static constexpr uint8_t PSTT_TXMOD_OUTPUTS_MODIFIABLE = 1 << 1;
static constexpr uint8_t PSTT_TXMOD_HAS_SIGHASH_SINGLE = 1 << 2;
// Bits 3-7 are reserved and must be 0.
static constexpr uint8_t PSTT_TXMOD_RESERVED_MASK = ~static_cast<uint8_t>(
    PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE | PSTT_TXMOD_HAS_SIGHASH_SINGLE);

// ---------------------------------------------------------------------
// PSTT_GLOBAL_XPUB keydata helpers (see pstt.cpp)
//
// The spec's 78-byte xpub keydata is BIP32's full serialization (4-byte
// version prefix + CExtPubKey's 74-byte encoding). CExtPubKey::Encode()
// only produces the 74 bytes; the version prefix must be affixed/checked
// separately against Params().Base58Prefix(EXT_PUBLIC_KEY) -- exactly one
// accepted prefix, not a {PROD, DEV} set (see pstt.cpp for the rationale).
// Do not use EncodeExtPubKey/DecodeExtPubKey (key_io.h) here -- those
// produce/consume a base58check *string*, the wrong shape for raw PSTT
// keydata.
// ---------------------------------------------------------------------

static constexpr size_t PSTT_XPUB_KEYDATA_SIZE = 4 + BIP32_EXTKEY_SIZE; // 78

std::vector<unsigned char> SerializeXpubKeyData(const CExtPubKey& xpub);
/** Throws std::ios_base::failure (naming the concrete prefix mismatch) if
 *  the 4-byte prefix isn't exactly Params().Base58Prefix(EXT_PUBLIC_KEY). */
CExtPubKey ParseXpubKeyData(const std::vector<unsigned char>& keydata);

/** Canonical dedup key for a PSTT_GLOBAL_XPUB entry: keydata bytes followed
 *  by the derivation path. Used by Merge() and joinpstt to avoid emitting
 *  duplicate PSTT_GLOBAL_XPUB entries, which Unserialize() rejects. */
std::vector<unsigned char> XpubEntryCanonicalBytes(const std::pair<CExtPubKey, std::vector<uint32_t>>& entry);

// ---------------------------------------------------------------------
// PSTTInput
// ---------------------------------------------------------------------

struct PSTTInput
{
    // Required -- these have no natural "empty" sentinel, hence explicit
    // presence flags rather than e.g. treating an all-zero txid as absent.
    uint256  previous_txid;
    uint32_t prevout_index = 0;
    bool     previous_txid_set = false;
    bool     prevout_index_set = false;

    // Optional, absence is the natural "empty"
    CTransactionRef utxo;             // PSTT_IN_UTXO -- full previous tx only
    CScript   redeem_script;          // PSTT_IN_REDEEM_SCRIPT (CP2SH: excludes color-id prefix)
    CScript   final_script_sig;       // PSTT_IN_FINAL_SCRIPTSIG
    std::map<CPubKey, std::vector<uint32_t>> hd_keypaths;   // PSTT_IN_BIP32_DERIVATION
    std::map<CKeyID, SigPair> partial_sigs;                 // PSTT_IN_PARTIAL_SIG
    boost::optional<int32_t> sighash_type;            // PSTT_IN_SIGHASH_TYPE
    boost::optional<uint32_t> sequence;               // PSTT_IN_SEQUENCE; default 0xFFFFFFFF if absent
    boost::optional<uint32_t> required_time_locktime;    // >= 500000000
    boost::optional<uint32_t> required_height_locktime;  // >0 and <500000000
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> ripemd160_preimages; // key len 20
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> sha256_preimages;    // key len 32
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> hash160_preimages;   // key len 20
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> hash256_preimages;   // key len 32
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> unknown; // PSTT_IN_PROPRIETARY + unrecognized

    bool IsNull() const;
    void FillSignatureData(SignatureData& sigdata) const;
    void FromSignatureData(const SignatureData& sigdata);
    void Merge(const PSTTInput& input);
    bool IsSane() const;

    template <typename Stream>
    void Serialize(Stream& s) const;
    template <typename Stream>
    void Unserialize(Stream& s);
};

// ---------------------------------------------------------------------
// PSTTOutput
// ---------------------------------------------------------------------

struct PSTTOutput
{
    boost::optional<CAmount> amount;  // PSTT_OUT_AMOUNT; required
    CScript   script;                 // PSTT_OUT_SCRIPT; required -- includes <color id> OP_COLOR prefix if colored
    CScript   redeem_script;          // PSTT_OUT_REDEEM_SCRIPT
    std::map<CPubKey, std::vector<uint32_t>> hd_keypaths; // PSTT_OUT_BIP32_DERIVATION
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> unknown; // PSTT_OUT_PROPRIETARY + unrecognized

    bool IsNull() const;
    void FillSignatureData(SignatureData& sigdata) const;
    void FromSignatureData(const SignatureData& sigdata);
    void Merge(const PSTTOutput& output);
    bool IsSane() const;   // amount present && script present

    template <typename Stream>
    void Serialize(Stream& s) const;
    template <typename Stream>
    void Unserialize(Stream& s);
};

// ---------------------------------------------------------------------
// PartiallySignedTapyrusTransaction
// ---------------------------------------------------------------------

struct PartiallySignedTapyrusTransaction
{
    boost::optional<int32_t>  tx_features;       // PSTT_GLOBAL_TX_FEATURES; required, any value accepted
    boost::optional<uint32_t> fallback_locktime; // PSTT_GLOBAL_FALLBACK_LOCKTIME; default 0
    boost::optional<uint8_t>  tx_modifiable;     // PSTT_GLOBAL_TX_MODIFIABLE bitfield; absent = not modifiable
    boost::optional<uint32_t> version;           // PSTT_GLOBAL_VERSION; default 0; must reject > 0 on parse
    std::vector<std::pair<CExtPubKey, std::vector<uint32_t>>> xpubs; // PSTT_GLOBAL_XPUB
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> unknown; // PSTT_GLOBAL_PROPRIETARY + unrecognized

    std::vector<PSTTInput>  inputs;
    std::vector<PSTTOutput> outputs;
    // PSTT_GLOBAL_INPUT_COUNT/OUTPUT_COUNT are NOT separate stored members --
    // always == inputs.size()/outputs.size() in memory. On the wire they tell
    // Unserialize how many input-map/output-map blocks follow; a mismatch
    // between the declared count and the stream's actual content is a parse
    // error.

    bool IsNull() const;
    void Merge(const PartiallySignedTapyrusTransaction& other);   // Combiner
    bool IsSane() const;
    uint256 GetIdentifier() const;
    bool HasSameIdentifierAs(const PartiallySignedTapyrusTransaction& other) const
    {
        return GetIdentifier() == other.GetIdentifier();
    }

    template <typename Stream>
    void Serialize(Stream& s) const;
    template <typename Stream>
    void Unserialize(Stream& s);
};

/** Determine the locktime a PartiallySignedTapyrusTransaction's materialized
 *  transaction must carry, per TIP-174's "Determining the Locktime" algorithm
 *  (see pstt.cpp). Returns false (leaving locktime_out unspecified) if no
 *  locktime kind is acceptable to every input that constrains one. */
bool ComputeLocktime(const PartiallySignedTapyrusTransaction& pstt, uint32_t& locktime_out);

/** Transaction Extractor. Requires every input to carry PSTT_IN_FINAL_SCRIPTSIG
 *  (throws std::runtime_error naming the first input missing it otherwise),
 *  computes the final locktime via ComputeLocktime, and materializes the
 *  fully signed transaction ready for broadcast. */
CMutableTransaction ExtractPSTT(const PartiallySignedTapyrusTransaction& pstt);

// ---------------------------------------------------------------------
// Signer result taxonomy
// ---------------------------------------------------------------------

enum class PSTTSignResult
{
    OK,
    MISSING_UTXO,
    UTXO_TXID_MISMATCH,
    PREVOUT_INDEX_OOB,
    REDEEM_SCRIPT_HASH_MISMATCH,
    SIGHASH_CONFLICT,
    SCHEME_CONFLICT,
    LOCKTIME_INVALID,
    SIGHASH_SINGLE_OOB,
};

// String forms returned by PSTTSignResultToString() are PSTT-internal and
// may be renamed; RPC callers must not treat them as a stable API contract
// beyond "this string is safe to surface in an error message."
std::string PSTTSignResultToString(PSTTSignResult result);

/** Sign one input of a PartiallySignedTapyrusTransaction, verifying that all
 *  provided data (UTXO, redeem script, sighash type, locktime) matches what
 *  is being signed, and that the added signature's scheme doesn't conflict
 *  with any signature already present on that input. */
PSTTSignResult SignPSTTInput(const SigningProvider& provider, const PartiallySignedTapyrusTransaction& pstt,
                              unsigned int index, SignatureData& sigdata, int sighash,
                              SignatureScheme sigScheme = SignatureScheme::ECDSA);

#endif // TAPYRUS_PSTT_H
