// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pstt.h>

#include <chainparams.h>
#include <coloridentifier.h>
#include <hash.h>
#include <key.h>
#include <script/interpreter.h>
#include <script/standard.h>
#include <streams.h>
#include <utilstrencodings.h>

#include <algorithm>
#include <set>

// =======================================================================
// PSTT_GLOBAL_XPUB keydata helpers
// =======================================================================

std::vector<unsigned char> SerializeXpubKeyData(const CExtPubKey& xpub)
{
    const std::vector<unsigned char>& prefix = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
    std::vector<unsigned char> out(prefix.begin(), prefix.end());
    unsigned char code[BIP32_EXTKEY_SIZE];
    xpub.Encode(code);
    out.insert(out.end(), code, code + BIP32_EXTKEY_SIZE);
    return out;
}

CExtPubKey ParseXpubKeyData(const std::vector<unsigned char>& keydata)
{
    if (keydata.size() != PSTT_XPUB_KEYDATA_SIZE) {
        throw std::ios_base::failure("PSTT_GLOBAL_XPUB keydata is not 78 bytes");
    }
    const std::vector<unsigned char>& expected_prefix = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
    std::vector<unsigned char> actual_prefix(keydata.begin(), keydata.begin() + 4);
    if (actual_prefix != expected_prefix) {
        throw std::ios_base::failure(
            "expected xpub prefix " + HexStr(expected_prefix) + ", got " + HexStr(actual_prefix));
    }
    CExtPubKey xpub;
    unsigned char code[BIP32_EXTKEY_SIZE];
    std::copy(keydata.begin() + 4, keydata.end(), code);
    xpub.Decode(code);
    return xpub;
}

// =======================================================================
// PSTTInput
// =======================================================================

bool PSTTInput::IsNull() const
{
    return !previous_txid_set && !prevout_index_set && !utxo && partial_sigs.empty() &&
           unknown.empty() && hd_keypaths.empty() && redeem_script.empty() && final_script_sig.empty();
}

void PSTTInput::FillSignatureData(SignatureData& sigdata) const
{
    if (!final_script_sig.empty()) {
        sigdata.scriptSig = final_script_sig;
        sigdata.complete = true;
    }
    if (sigdata.complete) {
        return;
    }

    sigdata.signatures.insert(partial_sigs.begin(), partial_sigs.end());
    if (!redeem_script.empty()) {
        sigdata.redeem_script = redeem_script;
    }
    for (const auto& key_pair : hd_keypaths) {
        sigdata.misc_pubkeys.emplace(key_pair.first.GetID(), key_pair.first);
    }
}

void PSTTInput::FromSignatureData(const SignatureData& sigdata)
{
    if (sigdata.complete) {
        partial_sigs.clear();
        hd_keypaths.clear();
        redeem_script.clear();

        if (!sigdata.scriptSig.empty()) {
            final_script_sig = sigdata.scriptSig;
        }
        return;
    }

    partial_sigs.insert(sigdata.signatures.begin(), sigdata.signatures.end());
    if (redeem_script.empty() && !sigdata.redeem_script.empty()) {
        redeem_script = sigdata.redeem_script;
    }
}

// Per-field-class Combiner conflict policy:
//  - PARTIAL_SIG / FINAL_SCRIPTSIG / UTXO           -> refuse (throw) on conflict
//  - REDEEM_SCRIPT / any BIP32_DERIVATION            -> must-match (throw on mismatch)
//  - everything else (incl. unknown/proprietary)      -> pick-first, no throw
void PSTTInput::Merge(const PSTTInput& input)
{
    // previous_txid / prevout_index are required-and-identical by construction
    // (both sides must already agree, or they wouldn't share a PSTT identifier --
    // HasSameIdentifierAs() is checked by the Combiner before Merge() is ever
    // called). Nothing to merge for these two fields.

    // UTXO: refuse tier.
    if (utxo && input.utxo) {
        if (utxo->GetHashMalFix() != input.utxo->GetHashMalFix()) {
            throw std::invalid_argument("PSTT_IN_UTXO conflict on Merge()");
        }
    } else if (!utxo && input.utxo) {
        utxo = input.utxo;
    }

    // FINAL_SCRIPTSIG: refuse tier.
    if (!final_script_sig.empty() && !input.final_script_sig.empty()) {
        if (final_script_sig != input.final_script_sig) {
            throw std::invalid_argument("PSTT_IN_FINAL_SCRIPTSIG conflict on Merge()");
        }
    } else if (final_script_sig.empty() && !input.final_script_sig.empty()) {
        final_script_sig = input.final_script_sig;
    }

    // PARTIAL_SIG: refuse tier, per-pubkey.
    for (const auto& kv : input.partial_sigs) {
        auto it = partial_sigs.find(kv.first);
        if (it != partial_sigs.end()) {
            if (it->second.second != kv.second.second) {
                throw std::invalid_argument("PSTT_IN_PARTIAL_SIG conflict on Merge()");
            }
        } else {
            partial_sigs.insert(kv);
        }
    }

    // REDEEM_SCRIPT: must-match tier.
    if (!redeem_script.empty() && !input.redeem_script.empty()) {
        if (redeem_script != input.redeem_script) {
            throw std::invalid_argument("PSTT_IN_REDEEM_SCRIPT mismatch on Merge()");
        }
    } else if (redeem_script.empty() && !input.redeem_script.empty()) {
        redeem_script = input.redeem_script;
    }

    // BIP32_DERIVATION: must-match tier, per-pubkey.
    for (const auto& kv : input.hd_keypaths) {
        auto it = hd_keypaths.find(kv.first);
        if (it != hd_keypaths.end()) {
            if (it->second != kv.second) {
                throw std::invalid_argument("PSTT_IN_BIP32_DERIVATION mismatch on Merge()");
            }
        } else {
            hd_keypaths.insert(kv);
        }
    }

    // Everything else: pick-first, no throw.
    if (!sighash_type && input.sighash_type) sighash_type = input.sighash_type;
    if (!sequence && input.sequence) sequence = input.sequence;
    if (!required_time_locktime && input.required_time_locktime) required_time_locktime = input.required_time_locktime;
    if (!required_height_locktime && input.required_height_locktime) required_height_locktime = input.required_height_locktime;
    for (const auto& kv : input.ripemd160_preimages) ripemd160_preimages.insert(kv);
    for (const auto& kv : input.sha256_preimages) sha256_preimages.insert(kv);
    for (const auto& kv : input.hash160_preimages) hash160_preimages.insert(kv);
    for (const auto& kv : input.hash256_preimages) hash256_preimages.insert(kv);
    for (const auto& kv : input.unknown) unknown.insert(kv);
}

bool PSTTInput::IsSane() const
{
    // previous_txid / output_index are required.
    if (!previous_txid_set || !prevout_index_set) return false;

    // No mixed ECDSA/Schnorr signatures on one input. Mirrors interpreter.cpp's
    // SCRIPT_ERR_MIXED_SCHEME_MULTISIG classification rule exactly: a 65-byte
    // signature (including its trailing sighash byte) is Schnorr, anything else
    // is ECDSA.
    boost::optional<SignatureScheme> scheme_seen;
    for (const auto& kv : partial_sigs) {
        const std::vector<unsigned char>& sig = kv.second.second;
        SignatureScheme this_scheme = (sig.size() == CPubKey::COMPACT_SIGNATURE_SIZE)
            ? SignatureScheme::SCHNORR : SignatureScheme::ECDSA;
        if (scheme_seen && *scheme_seen != this_scheme) return false;
        scheme_seen = this_scheme;
    }

    return true;
}

template <typename Stream>
void PSTTInput::Serialize(Stream& s) const
{
    if (previous_txid_set) {
        SerializeToVector(s, PSTT_IN_PREVIOUS_TXID);
        SerializeToVector(s, previous_txid);
    }
    if (prevout_index_set) {
        SerializeToVector(s, PSTT_IN_OUTPUT_INDEX);
        SerializeToVector(s, prevout_index);
    }
    if (utxo) {
        SerializeToVector(s, PSTT_IN_UTXO);
        SerializeToVector(s, utxo);
    }

    if (final_script_sig.empty()) {
        for (const auto& sig_pair : partial_sigs) {
            SerializeToVector(s, PSTT_IN_PARTIAL_SIG, MakeSpan(sig_pair.second.first));
            s << sig_pair.second.second;
        }

        if (sighash_type) {
            SerializeToVector(s, PSTT_IN_SIGHASH_TYPE);
            SerializeToVector(s, *sighash_type);
        }

        if (!redeem_script.empty()) {
            SerializeToVector(s, PSTT_IN_REDEEM_SCRIPT);
            s << redeem_script;
        }

        SerializeHDKeypaths(s, hd_keypaths, PSTT_IN_BIP32_DERIVATION);

        for (const auto& kv : ripemd160_preimages) {
            SerializeToVector(s, PSTT_IN_RIPEMD160, MakeSpan(kv.first));
            s << kv.second;
        }
        for (const auto& kv : sha256_preimages) {
            SerializeToVector(s, PSTT_IN_SHA256, MakeSpan(kv.first));
            s << kv.second;
        }
        for (const auto& kv : hash160_preimages) {
            SerializeToVector(s, PSTT_IN_HASH160, MakeSpan(kv.first));
            s << kv.second;
        }
        for (const auto& kv : hash256_preimages) {
            SerializeToVector(s, PSTT_IN_HASH256, MakeSpan(kv.first));
            s << kv.second;
        }
    }

    if (!final_script_sig.empty()) {
        SerializeToVector(s, PSTT_IN_FINAL_SCRIPTSIG);
        s << final_script_sig;
    }

    if (sequence) {
        SerializeToVector(s, PSTT_IN_SEQUENCE);
        SerializeToVector(s, *sequence);
    }
    if (required_time_locktime) {
        SerializeToVector(s, PSTT_IN_REQUIRED_TIME_LOCKTIME);
        SerializeToVector(s, *required_time_locktime);
    }
    if (required_height_locktime) {
        SerializeToVector(s, PSTT_IN_REQUIRED_HEIGHT_LOCKTIME);
        SerializeToVector(s, *required_height_locktime);
    }

    for (const auto& entry : unknown) {
        s << entry.first;
        s << entry.second;
    }

    s << PSTT_SEPARATOR;
}

template <typename Stream>
void PSTTInput::Unserialize(Stream& s)
{
    while (true) {
        if (s.empty()) throw std::ios_base::failure("PSTT input map is missing its terminating separator");
        std::vector<unsigned char> key;
        s >> key;
        if (key.empty()) return; // separator found

        unsigned char type = key[0];
        switch (type) {
            case PSTT_IN_UTXO:
            {
                if (utxo) throw std::ios_base::failure("Duplicate Key, PSTT_IN_UTXO already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_UTXO key is more than one byte type");
                UnserializeFromVector(s, utxo);
                break;
            }
            case PSTT_IN_PARTIAL_SIG:
            {
                if (key.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 1 && key.size() != CPubKey::PUBLIC_KEY_SIZE + 1) {
                    throw std::ios_base::failure("PSTT_IN_PARTIAL_SIG keydata must be a 33- or 65-byte public key");
                }
                CPubKey pubkey(key.begin() + 1, key.end());
                if (!pubkey.IsFullyValid()) throw std::ios_base::failure("Invalid pubkey");
                if (partial_sigs.count(pubkey.GetID()) > 0) {
                    throw std::ios_base::failure("Duplicate Key, PSTT_IN_PARTIAL_SIG for pubkey already provided");
                }
                std::vector<unsigned char> sig;
                s >> sig;
                // valuedata: DER signature + 1 hashtype byte (variable, ~71-73 total incl. that
                // byte), or exactly a 65-byte Schnorr sig + 1 hashtype byte (64+1). No third
                // length is legal.
                if (sig.size() != CPubKey::COMPACT_SIGNATURE_SIZE &&
                    (sig.size() < 9 || sig.size() > CPubKey::SIGNATURE_SIZE + 1)) {
                    throw std::ios_base::failure("PSTT_IN_PARTIAL_SIG valuedata has an invalid length");
                }
                partial_sigs.emplace(pubkey.GetID(), SigPair(pubkey, std::move(sig)));
                break;
            }
            case PSTT_IN_SIGHASH_TYPE:
                if (sighash_type) throw std::ios_base::failure("Duplicate Key, PSTT_IN_SIGHASH_TYPE already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_SIGHASH_TYPE key is more than one byte type");
                {
                    int32_t v;
                    UnserializeFromVector(s, v);
                    sighash_type = v;
                }
                break;
            case PSTT_IN_REDEEM_SCRIPT:
                if (!redeem_script.empty()) throw std::ios_base::failure("Duplicate Key, PSTT_IN_REDEEM_SCRIPT already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_REDEEM_SCRIPT key is more than one byte type");
                s >> redeem_script;
                break;
            case PSTT_IN_BIP32_DERIVATION:
                DeserializeHDKeypaths(s, key, hd_keypaths);
                break;
            case PSTT_IN_FINAL_SCRIPTSIG:
                if (!final_script_sig.empty()) throw std::ios_base::failure("Duplicate Key, PSTT_IN_FINAL_SCRIPTSIG already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_FINAL_SCRIPTSIG key is more than one byte type");
                s >> final_script_sig;
                break;
            case PSTT_IN_RIPEMD160:
            case PSTT_IN_HASH160:
            {
                auto& map_ref = (type == PSTT_IN_RIPEMD160) ? ripemd160_preimages : hash160_preimages;
                if (key.size() != 20 + 1) throw std::ios_base::failure("Preimage keydata must be 20 bytes");
                std::vector<unsigned char> hash_key(key.begin() + 1, key.end());
                if (map_ref.count(hash_key) > 0) throw std::ios_base::failure("Duplicate Key, preimage already provided");
                std::vector<unsigned char> preimage;
                s >> preimage;
                map_ref.emplace(std::move(hash_key), std::move(preimage));
                break;
            }
            case PSTT_IN_SHA256:
            case PSTT_IN_HASH256:
            {
                auto& map_ref = (type == PSTT_IN_SHA256) ? sha256_preimages : hash256_preimages;
                if (key.size() != 32 + 1) throw std::ios_base::failure("Preimage keydata must be 32 bytes");
                std::vector<unsigned char> hash_key(key.begin() + 1, key.end());
                if (map_ref.count(hash_key) > 0) throw std::ios_base::failure("Duplicate Key, preimage already provided");
                std::vector<unsigned char> preimage;
                s >> preimage;
                map_ref.emplace(std::move(hash_key), std::move(preimage));
                break;
            }
            case PSTT_IN_PREVIOUS_TXID:
                if (previous_txid_set) throw std::ios_base::failure("Duplicate Key, PSTT_IN_PREVIOUS_TXID already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_PREVIOUS_TXID key is more than one byte type");
                UnserializeFromVector(s, previous_txid);
                previous_txid_set = true;
                break;
            case PSTT_IN_OUTPUT_INDEX:
                if (prevout_index_set) throw std::ios_base::failure("Duplicate Key, PSTT_IN_OUTPUT_INDEX already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_OUTPUT_INDEX key is more than one byte type");
                UnserializeFromVector(s, prevout_index);
                prevout_index_set = true;
                break;
            case PSTT_IN_SEQUENCE:
                if (sequence) throw std::ios_base::failure("Duplicate Key, PSTT_IN_SEQUENCE already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_SEQUENCE key is more than one byte type");
                {
                    uint32_t v;
                    UnserializeFromVector(s, v);
                    sequence = v;
                }
                break;
            case PSTT_IN_REQUIRED_TIME_LOCKTIME:
            {
                if (required_time_locktime) throw std::ios_base::failure("Duplicate Key, PSTT_IN_REQUIRED_TIME_LOCKTIME already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_REQUIRED_TIME_LOCKTIME key is more than one byte type");
                uint32_t v;
                UnserializeFromVector(s, v);
                if (v < 500000000) throw std::ios_base::failure("PSTT_IN_REQUIRED_TIME_LOCKTIME must be greater than or equal to 500000000");
                required_time_locktime = v;
                break;
            }
            case PSTT_IN_REQUIRED_HEIGHT_LOCKTIME:
            {
                if (required_height_locktime) throw std::ios_base::failure("Duplicate Key, PSTT_IN_REQUIRED_HEIGHT_LOCKTIME already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_IN_REQUIRED_HEIGHT_LOCKTIME key is more than one byte type");
                uint32_t v;
                UnserializeFromVector(s, v);
                if (v == 0) throw std::ios_base::failure("PSTT_IN_REQUIRED_HEIGHT_LOCKTIME must be greater than 0");
                if (v >= 500000000) throw std::ios_base::failure("PSTT_IN_REQUIRED_HEIGHT_LOCKTIME must be less than 500000000");
                required_height_locktime = v;
                break;
            }
            // Reserved and must be rejected, not silently treated as unknown:
            // 0x01 witness UTXO, 0x05 witness script, 0x08 finalized scriptWitness,
            // 0x09 proof-of-reserves commitment, 0x13-0x18 Taproot fields (BIP-371).
            // See doc/tapyrus/pstt.md.
            case 0x01: case 0x05: case 0x08: case 0x09:
            case 0x13: case 0x14: case 0x15: case 0x16: case 0x17: case 0x18:
                throw std::ios_base::failure("Reserved input key type used");
            case PSTT_IN_PROPRIETARY:
            default:
                if (unknown.count(key) > 0) throw std::ios_base::failure("Duplicate Key, key for unknown value already provided");
                std::vector<unsigned char> val_bytes;
                s >> val_bytes;
                unknown.emplace(std::move(key), std::move(val_bytes));
                break;
        }
    }
}

// =======================================================================
// PSTTOutput
// =======================================================================

bool PSTTOutput::IsNull() const
{
    return !amount && script.empty() && redeem_script.empty() && hd_keypaths.empty() && unknown.empty();
}

void PSTTOutput::FillSignatureData(SignatureData& sigdata) const
{
    if (!redeem_script.empty()) {
        sigdata.redeem_script = redeem_script;
    }
    for (const auto& key_pair : hd_keypaths) {
        sigdata.misc_pubkeys.emplace(key_pair.first.GetID(), key_pair.first);
    }
}

void PSTTOutput::FromSignatureData(const SignatureData& sigdata)
{
    if (redeem_script.empty() && !sigdata.redeem_script.empty()) {
        redeem_script = sigdata.redeem_script;
    }
}

void PSTTOutput::Merge(const PSTTOutput& output)
{
    // REDEEM_SCRIPT: must-match tier.
    if (!redeem_script.empty() && !output.redeem_script.empty()) {
        if (redeem_script != output.redeem_script) {
            throw std::invalid_argument("PSTT_OUT_REDEEM_SCRIPT mismatch on Merge()");
        }
    } else if (redeem_script.empty() && !output.redeem_script.empty()) {
        redeem_script = output.redeem_script;
    }

    // BIP32_DERIVATION: must-match tier.
    for (const auto& kv : output.hd_keypaths) {
        auto it = hd_keypaths.find(kv.first);
        if (it != hd_keypaths.end()) {
            if (it->second != kv.second) {
                throw std::invalid_argument("PSTT_OUT_BIP32_DERIVATION mismatch on Merge()");
            }
        } else {
            hd_keypaths.insert(kv);
        }
    }

    for (const auto& kv : output.unknown) unknown.insert(kv);
}

bool PSTTOutput::IsSane() const
{
    return amount.is_initialized() && !script.empty();
}

template <typename Stream>
void PSTTOutput::Serialize(Stream& s) const
{
    if (amount) {
        SerializeToVector(s, PSTT_OUT_AMOUNT);
        SerializeToVector(s, *amount);
    }
    if (!script.empty()) {
        SerializeToVector(s, PSTT_OUT_SCRIPT);
        s << script;
    }
    if (!redeem_script.empty()) {
        SerializeToVector(s, PSTT_OUT_REDEEM_SCRIPT);
        s << redeem_script;
    }

    SerializeHDKeypaths(s, hd_keypaths, PSTT_OUT_BIP32_DERIVATION);

    for (const auto& entry : unknown) {
        s << entry.first;
        s << entry.second;
    }

    s << PSTT_SEPARATOR;
}

template <typename Stream>
void PSTTOutput::Unserialize(Stream& s)
{
    while (true) {
        if (s.empty()) throw std::ios_base::failure("PSTT output map is missing its terminating separator");
        std::vector<unsigned char> key;
        s >> key;
        if (key.empty()) return; // separator found

        unsigned char type = key[0];
        switch (type) {
            case PSTT_OUT_REDEEM_SCRIPT:
                if (!redeem_script.empty()) throw std::ios_base::failure("Duplicate Key, PSTT_OUT_REDEEM_SCRIPT already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_OUT_REDEEM_SCRIPT key is more than one byte type");
                s >> redeem_script;
                break;
            case PSTT_OUT_BIP32_DERIVATION:
                DeserializeHDKeypaths(s, key, hd_keypaths);
                break;
            case PSTT_OUT_AMOUNT:
                if (amount) throw std::ios_base::failure("Duplicate Key, PSTT_OUT_AMOUNT already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_OUT_AMOUNT key is more than one byte type");
                {
                    CAmount v;
                    UnserializeFromVector(s, v);
                    amount = v;
                }
                break;
            case PSTT_OUT_SCRIPT:
                if (!script.empty()) throw std::ios_base::failure("Duplicate Key, PSTT_OUT_SCRIPT already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_OUT_SCRIPT key is more than one byte type");
                s >> script;
                break;
            // Reserved and must be rejected: 0x01 witness script, 0x05-0x07
            // Taproot fields (BIP-371). See doc/tapyrus/pstt.md.
            case 0x01:
            case 0x05: case 0x06: case 0x07:
                throw std::ios_base::failure("Reserved output key type used");
            case PSTT_OUT_PROPRIETARY:
            default:
                if (unknown.count(key) > 0) throw std::ios_base::failure("Duplicate Key, key for unknown value already provided");
                std::vector<unsigned char> val_bytes;
                s >> val_bytes;
                unknown.emplace(std::move(key), std::move(val_bytes));
                break;
        }
    }
}

// =======================================================================
// PartiallySignedTapyrusTransaction
// =======================================================================

bool PartiallySignedTapyrusTransaction::IsNull() const
{
    return !tx_features && inputs.empty() && outputs.empty() && unknown.empty() && xpubs.empty();
}

// xpubs dedup: a temporary set-like lookup keyed on the entry's canonical
// byte form (78-byte xpub + derivation path), built just for the duration
// of this call, so appending stays close to linear rather than O(n^2) for
// large xpub counts.
std::vector<unsigned char> XpubEntryCanonicalBytes(const std::pair<CExtPubKey, std::vector<uint32_t>>& entry)
{
    std::vector<unsigned char> out = SerializeXpubKeyData(entry.first);
    for (uint32_t v : entry.second) {
        out.push_back(static_cast<unsigned char>(v & 0xff));
        out.push_back(static_cast<unsigned char>((v >> 8) & 0xff));
        out.push_back(static_cast<unsigned char>((v >> 16) & 0xff));
        out.push_back(static_cast<unsigned char>((v >> 24) & 0xff));
    }
    return out;
}

void PartiallySignedTapyrusTransaction::Merge(const PartiallySignedTapyrusTransaction& other)
{
    if (inputs.size() != other.inputs.size() || outputs.size() != other.outputs.size()) {
        throw std::invalid_argument("PSTT Merge() with mismatched input/output counts");
    }

    for (unsigned int i = 0; i < inputs.size(); ++i) {
        inputs[i].Merge(other.inputs[i]);
    }
    for (unsigned int i = 0; i < outputs.size(); ++i) {
        outputs[i].Merge(other.outputs[i]);
    }

    std::set<std::vector<unsigned char>> seen;
    for (const auto& entry : xpubs) seen.insert(XpubEntryCanonicalBytes(entry));
    for (const auto& entry : other.xpubs) {
        std::vector<unsigned char> canon = XpubEntryCanonicalBytes(entry);
        if (seen.insert(canon).second) {
            xpubs.push_back(entry);
        }
    }

    // Global unknown/proprietary fields: pick-first tier.
    for (const auto& kv : other.unknown) unknown.insert(kv);
}

bool PartiallySignedTapyrusTransaction::IsSane() const
{
    for (const PSTTInput& input : inputs) {
        if (!input.IsSane()) return false;
        if (input.utxo && input.prevout_index_set && input.prevout_index >= input.utxo->vout.size()) {
            return false;
        }
        // Deliberately NOT checked here: input.utxo->GetHashMalFix() ==
        // input.previous_txid. Per TIP-174's own fixtures (see
        // test/functional/data/tip174_invalid.json, "utxo-txid-mismatch"),
        // a PSTT_IN_UTXO/PSTT_IN_PREVIOUS_TXID mismatch is a Signer-role
        // rule violation, not a structural/parse failure -- it must still
        // decode successfully, and only SignPSTTInput() is required to
        // reject it. See SignPSTTInput() for the actual enforcement.
    }
    for (const PSTTOutput& output : outputs) {
        if (!output.IsSane()) return false;
    }
    if (tx_modifiable && (*tx_modifiable & PSTT_TXMOD_RESERVED_MASK) != 0) {
        return false; // reserved PSTT_GLOBAL_TX_MODIFIABLE bits must be 0
    }
    return true;
}

// PSTT_GLOBAL_INPUT_COUNT/OUTPUT_COUNT's value is itself "<compact size uint>",
// which -- like every other value in this KV-map format -- also needs the
// uniform outer value-length prefix. Streaming a plain byte vector
// already produces exactly that envelope (length-prefix + raw bytes), so
// build the inner compact-size encoding into one first rather than writing
// the count directly (which would omit the outer length prefix entirely).
template <typename Stream>
static void SerializeCompactSizeValue(Stream& s, uint64_t v)
{
    CDataStream inner(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(inner, v);
    std::vector<unsigned char> bytes(inner.begin(), inner.end());
    s << bytes;
}

template <typename Stream>
static uint64_t UnserializeCompactSizeValue(Stream& s)
{
    std::vector<unsigned char> bytes;
    s >> bytes;
    CDataStream inner(bytes, SER_NETWORK, PROTOCOL_VERSION);
    uint64_t v = ReadCompactSize(inner);
    if (!inner.empty()) {
        throw std::ios_base::failure("Compact-size value has trailing bytes");
    }
    return v;
}

template <typename Stream>
void PartiallySignedTapyrusTransaction::Serialize(Stream& s) const
{
    s << PSTT_MAGIC_BYTES;

    // Required fields first (PSTT_GLOBAL_TX_FEATURES/_INPUT_COUNT/_OUTPUT_COUNT
    // are the only globals a well-formed PSTT must always carry -- see the
    // "required" annotations in the constants above), then everything else.
    if (tx_features) {
        SerializeToVector(s, PSTT_GLOBAL_TX_FEATURES);
        SerializeToVector(s, *tx_features);
    }
    SerializeToVector(s, PSTT_GLOBAL_INPUT_COUNT);
    SerializeCompactSizeValue(s, inputs.size());
    SerializeToVector(s, PSTT_GLOBAL_OUTPUT_COUNT);
    SerializeCompactSizeValue(s, outputs.size());

    if (version) {
        SerializeToVector(s, PSTT_GLOBAL_VERSION);
        SerializeToVector(s, *version);
    }

    for (const auto& entry : xpubs) {
        std::vector<unsigned char> keydata = SerializeXpubKeyData(entry.first);
        SerializeToVector(s, PSTT_GLOBAL_XPUB, MakeSpan(keydata));
        WriteCompactSize(s, entry.second.size() * sizeof(uint32_t));
        for (uint32_t v : entry.second) s << v;
    }

    if (fallback_locktime) {
        SerializeToVector(s, PSTT_GLOBAL_FALLBACK_LOCKTIME);
        SerializeToVector(s, *fallback_locktime);
    }
    if (tx_modifiable) {
        SerializeToVector(s, PSTT_GLOBAL_TX_MODIFIABLE);
        SerializeToVector(s, *tx_modifiable);
    }

    for (const auto& entry : unknown) {
        s << entry.first;
        s << entry.second;
    }

    s << PSTT_SEPARATOR;

    for (const PSTTInput& input : inputs) s << input;
    for (const PSTTOutput& output : outputs) s << output;
}

template <typename Stream>
void PartiallySignedTapyrusTransaction::Unserialize(Stream& s)
{
    uint8_t magic[5];
    s >> magic;
    if (!std::equal(magic, magic + 5, PSTT_MAGIC_BYTES)) {
        throw std::ios_base::failure("Invalid PSTT magic bytes");
    }

    boost::optional<uint64_t> input_count;
    boost::optional<uint64_t> output_count;
    std::set<std::vector<unsigned char>> seen_xpubs;

    while (true) {
        if (s.empty()) throw std::ios_base::failure("PSTT global map is missing its terminating separator");
        std::vector<unsigned char> key;
        s >> key;
        if (key.empty()) break; // separator found

        unsigned char type = key[0];
        switch (type) {
            case 0x00:
                throw std::ios_base::failure("Global type value 0x00 is reserved and must not be used");
            // Required fields first (mirrors Serialize()'s field order) --
            // PSTT_GLOBAL_TX_FEATURES/_INPUT_COUNT/_OUTPUT_COUNT are the only
            // globals a well-formed PSTT must always carry.
            case PSTT_GLOBAL_TX_FEATURES:
                if (tx_features) throw std::ios_base::failure("Duplicate Key, PSTT_GLOBAL_TX_FEATURES already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_GLOBAL_TX_FEATURES key is more than one byte type");
                {
                    int32_t v;
                    UnserializeFromVector(s, v);
                    tx_features = v; // any value accepted, see doc/tapyrus/pstt.md
                }
                break;
            case PSTT_GLOBAL_INPUT_COUNT:
                if (input_count) throw std::ios_base::failure("Duplicate Key, PSTT_GLOBAL_INPUT_COUNT already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_GLOBAL_INPUT_COUNT key is more than one byte type");
                input_count = UnserializeCompactSizeValue(s);
                break;
            case PSTT_GLOBAL_OUTPUT_COUNT:
                if (output_count) throw std::ios_base::failure("Duplicate Key, PSTT_GLOBAL_OUTPUT_COUNT already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_GLOBAL_OUTPUT_COUNT key is more than one byte type");
                output_count = UnserializeCompactSizeValue(s);
                break;
            // Everything else.
            case PSTT_GLOBAL_VERSION:
                if (version) throw std::ios_base::failure("Duplicate Key, PSTT_GLOBAL_VERSION already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_GLOBAL_VERSION key is more than one byte type");
                {
                    uint32_t v;
                    UnserializeFromVector(s, v);
                    if (v > 0) throw std::ios_base::failure("PSTT_GLOBAL_VERSION greater than 0 is not supported");
                    version = v;
                }
                break;
            case PSTT_GLOBAL_XPUB:
            {
                if (key.size() != 1 + PSTT_XPUB_KEYDATA_SIZE) {
                    throw std::ios_base::failure("PSTT_GLOBAL_XPUB keydata is not 78 bytes");
                }
                std::vector<unsigned char> xpub_keydata(key.begin() + 1, key.end());
                if (seen_xpubs.count(xpub_keydata) > 0) {
                    throw std::ios_base::failure("Duplicate Key, PSTT_GLOBAL_XPUB already provided");
                }
                seen_xpubs.insert(xpub_keydata);
                CExtPubKey xpub = ParseXpubKeyData(xpub_keydata);

                uint64_t value_len = ReadCompactSize(s);
                if (value_len < sizeof(uint32_t)) throw std::ios_base::failure("XPUB value must contain at least a 4-byte fingerprint");
                if (value_len % sizeof(uint32_t) != 0) throw std::ios_base::failure("XPUB value length is not a multiple of 4");
                std::vector<uint32_t> path;
                for (unsigned int i = 0; i < value_len; i += sizeof(uint32_t)) {
                    uint32_t v;
                    s >> v;
                    path.push_back(v);
                }
                xpubs.emplace_back(xpub, std::move(path));
                break;
            }
            case PSTT_GLOBAL_FALLBACK_LOCKTIME:
                if (fallback_locktime) throw std::ios_base::failure("Duplicate Key, PSTT_GLOBAL_FALLBACK_LOCKTIME already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_GLOBAL_FALLBACK_LOCKTIME key is more than one byte type");
                {
                    uint32_t v;
                    UnserializeFromVector(s, v);
                    fallback_locktime = v;
                }
                break;
            case PSTT_GLOBAL_TX_MODIFIABLE:
                if (tx_modifiable) throw std::ios_base::failure("Duplicate Key, PSTT_GLOBAL_TX_MODIFIABLE already provided");
                if (key.size() != 1) throw std::ios_base::failure("PSTT_GLOBAL_TX_MODIFIABLE key is more than one byte type");
                {
                    uint8_t v;
                    UnserializeFromVector(s, v);
                    tx_modifiable = v;
                }
                break;
            case PSTT_GLOBAL_PROPRIETARY:
            default:
                if (unknown.count(key) > 0) throw std::ios_base::failure("Duplicate Key, key for unknown value already provided");
                std::vector<unsigned char> val_bytes;
                s >> val_bytes;
                unknown.emplace(std::move(key), std::move(val_bytes));
                break;
        }
    }

    if (!tx_features) throw std::ios_base::failure("PSTT_GLOBAL_TX_FEATURES is required");
    if (!input_count) throw std::ios_base::failure("PSTT_GLOBAL_INPUT_COUNT is required");
    if (!output_count) throw std::ios_base::failure("PSTT_GLOBAL_OUTPUT_COUNT is required");

    for (uint64_t i = 0; i < *input_count; ++i) {
        if (s.empty()) throw std::ios_base::failure("Unexpected end of data while reading PSTT inputs");
        PSTTInput input;
        s >> input;
        inputs.push_back(std::move(input));
    }
    for (uint64_t i = 0; i < *output_count; ++i) {
        if (s.empty()) throw std::ios_base::failure("Unexpected end of data while reading PSTT outputs");
        PSTTOutput output;
        s >> output;
        outputs.push_back(std::move(output));
    }
    if (!s.empty()) {
        throw std::ios_base::failure("Unexpected data after the declared number of inputs/outputs");
    }

    if (!IsSane()) {
        throw std::ios_base::failure("PSTT is not sane.");
    }
}

// Explicit instantiation for the stream types this codebase actually uses --
// needed because Serialize/Unserialize are defined out-of-line from the
// class body (unlike the old PSBT code's inline-in-header templates).
template void PSTTInput::Serialize(CDataStream&) const;
template void PSTTInput::Unserialize(CDataStream&);
template void PSTTOutput::Serialize(CDataStream&) const;
template void PSTTOutput::Unserialize(CDataStream&);
template void PartiallySignedTapyrusTransaction::Serialize(CDataStream&) const;
template void PartiallySignedTapyrusTransaction::Unserialize(CDataStream&);

// =======================================================================
// Locktime, Identifier, and the shared tx-materialization helper.
// See doc/tapyrus/pstt.md for the locktime and identifier algorithms.
// =======================================================================

bool ComputeLocktime(const PartiallySignedTapyrusTransaction& pstt, uint32_t& locktime_out)
{
    bool any_constrains = false;
    for (const PSTTInput& input : pstt.inputs) {
        if (input.required_time_locktime || input.required_height_locktime) {
            any_constrains = true;
            break;
        }
    }
    if (!any_constrains) {
        locktime_out = pstt.fallback_locktime.value_or(0);
        return true;
    }

    bool height_possible = true;
    bool time_possible = true;
    for (const PSTTInput& input : pstt.inputs) {
        bool has_height = input.required_height_locktime.is_initialized();
        bool has_time = input.required_time_locktime.is_initialized();
        if (!has_height && !has_time) continue; // no constraint from this input
        if (!has_height) height_possible = false;
        if (!has_time) time_possible = false;
    }

    if (!height_possible && !time_possible) return false;

    bool use_height = height_possible; // both possible -> prefer height
    uint32_t chosen = 0;
    bool have_any = false;
    for (const PSTTInput& input : pstt.inputs) {
        const boost::optional<uint32_t>& field = use_height ? input.required_height_locktime : input.required_time_locktime;
        if (field) {
            chosen = have_any ? std::max(chosen, *field) : *field;
            have_any = true;
        }
    }
    if (!have_any) return false;
    locktime_out = chosen;
    return true;
}

namespace {

// Not exposed in pstt.h. force_zero_sequence is true only for GetIdentifier()
// (spec: identification zeroes all sequence numbers). use_final_scriptsig is
// true only for extraction (uses PSTT_IN_FINAL_SCRIPTSIG; false leaves
// scriptSig empty, used for signature-hash computation and IsSane()'s
// locktime pre-check).
CMutableTransaction MaterializeTransaction(const PartiallySignedTapyrusTransaction& pstt, uint32_t locktime,
                                            bool force_zero_sequence, bool use_final_scriptsig)
{
    CMutableTransaction mtx;
    mtx.nFeatures = pstt.tx_features.value_or(CTransaction::CURRENT_FEATURES);
    mtx.nLockTime = locktime;

    for (const PSTTInput& input : pstt.inputs) {
        CTxIn txin;
        txin.prevout = COutPoint(input.previous_txid, input.prevout_index);
        if (force_zero_sequence) {
            txin.nSequence = 0;
        } else {
            txin.nSequence = input.sequence.value_or(CTxIn::SEQUENCE_FINAL);
        }
        if (use_final_scriptsig) {
            txin.scriptSig = input.final_script_sig;
        }
        mtx.vin.push_back(std::move(txin));
    }

    for (const PSTTOutput& output : pstt.outputs) {
        mtx.vout.emplace_back(output.amount.value_or(0), output.script);
    }

    return mtx;
}

} // namespace

uint256 PartiallySignedTapyrusTransaction::GetIdentifier() const
{
    uint32_t locktime;
    if (!ComputeLocktime(*this, locktime)) {
        throw std::runtime_error("PSTT has no valid locktime; cannot compute identifier");
    }
    return MaterializeTransaction(*this, locktime, /*force_zero_sequence=*/true, /*use_final_scriptsig=*/false).GetHashMalFix();
}

CMutableTransaction ExtractPSTT(const PartiallySignedTapyrusTransaction& pstt)
{
    if (!pstt.IsSane()) {
        throw std::runtime_error("PSTT is not sane; cannot extract");
    }
    for (unsigned int i = 0; i < pstt.inputs.size(); ++i) {
        if (pstt.inputs[i].final_script_sig.empty()) {
            throw std::runtime_error("Input " + std::to_string(i) + " is missing PSTT_IN_FINAL_SCRIPTSIG; cannot extract");
        }
    }
    uint32_t locktime;
    if (!ComputeLocktime(pstt, locktime)) {
        throw std::runtime_error("PSTT has no valid locktime; cannot extract");
    }
    return MaterializeTransaction(pstt, locktime, /*force_zero_sequence=*/false, /*use_final_scriptsig=*/true);
}

// =======================================================================
// Signer
// =======================================================================

std::string PSTTSignResultToString(PSTTSignResult result)
{
    switch (result) {
        case PSTTSignResult::OK: return "OK";
        case PSTTSignResult::MISSING_UTXO: return "input has no PSTT_IN_UTXO";
        case PSTTSignResult::UTXO_TXID_MISMATCH: return "PSTT_IN_UTXO txid does not match PSTT_IN_PREVIOUS_TXID";
        case PSTTSignResult::PREVOUT_INDEX_OOB: return "PSTT_IN_OUTPUT_INDEX is out of range for PSTT_IN_UTXO";
        case PSTTSignResult::REDEEM_SCRIPT_HASH_MISMATCH: return "redeem script does not hash to the committed value";
        case PSTTSignResult::SIGHASH_CONFLICT: return "requested sighash type conflicts with PSTT_IN_SIGHASH_TYPE";
        case PSTTSignResult::SCHEME_CONFLICT: return "signature scheme conflicts with an existing signature on this input";
        case PSTTSignResult::LOCKTIME_INVALID: return "no locktime is acceptable to every constraining input";
        case PSTTSignResult::SIGHASH_SINGLE_OOB: return "SIGHASH_SINGLE used on an input with no corresponding output";
    }
    return "unknown PSTTSignResult";
}

static bool ExtractRedeemScriptHash(const CScript& scriptPubKey, uint160& hash_out)
{
    if (scriptPubKey.IsPayToScriptHash()) {
        // OP_HASH160 <20-byte hash> OP_EQUAL
        hash_out = uint160(std::vector<unsigned char>(scriptPubKey.begin() + 2, scriptPubKey.begin() + 22));
        return true;
    }
    if (scriptPubKey.IsColoredPayToScriptHash()) {
        // <COLOR identifier> OP_COLOR OP_HASH160 <20-byte hash> OP_EQUAL -- skip the
        // 33-byte color-id prefix (push opcode + 33 bytes payload = 34 bytes) before
        // the OP_HASH160/hash/OP_EQUAL suffix that mirrors plain P2SH.
        hash_out = uint160(std::vector<unsigned char>(scriptPubKey.begin() + 37, scriptPubKey.begin() + 57));
        return true;
    }
    return false;
}

PSTTSignResult SignPSTTInput(const SigningProvider& provider, const PartiallySignedTapyrusTransaction& pstt,
                              unsigned int index, SignatureData& sigdata, int sighash, SignatureScheme sigScheme)
{
    const PSTTInput& input = pstt.inputs.at(index);

    // Already finalized: nothing to do.
    if (!input.final_script_sig.empty()) {
        return PSTTSignResult::OK;
    }

    // Must not sign an input lacking a UTXO.
    if (!input.utxo) {
        return PSTTSignResult::MISSING_UTXO;
    }
    // Must verify the UTXO's txid matches PSTT_IN_PREVIOUS_TXID. Deliberately
    // NOT checked at Unserialize()/IsSane() time -- per TIP-174's own fixtures
    // (test/functional/data/tip174_invalid.json, "utxo-txid-mismatch"), this
    // is a Signer-role rule violation, not a structural one: a PSTT carrying
    // a mismatch must still decode successfully, and only signing it must
    // fail. This is the sole point of enforcement.
    if (input.utxo->GetHashMalFix() != input.previous_txid) {
        return PSTTSignResult::UTXO_TXID_MISMATCH;
    }
    if (input.prevout_index >= input.utxo->vout.size()) {
        return PSTTSignResult::PREVOUT_INDEX_OOB;
    }

    const CTxOut& utxo_out = input.utxo->vout[input.prevout_index];

    // Must verify the redeem script hashes to the value committed in the
    // scriptPubKey, accounting for CP2SH's color-id prefix.
    if (!input.redeem_script.empty()) {
        uint160 expected_hash;
        if (!ExtractRedeemScriptHash(utxo_out.scriptPubKey, expected_hash)) {
            return PSTTSignResult::REDEEM_SCRIPT_HASH_MISMATCH;
        }
        if (CScriptID(input.redeem_script) != CScriptID(expected_hash)) {
            return PSTTSignResult::REDEEM_SCRIPT_HASH_MISMATCH;
        }
    }

    // Must use the sighash type in PSTT_IN_SIGHASH_TYPE if present.
    if (input.sighash_type && *input.sighash_type != sighash) {
        return PSTTSignResult::SIGHASH_CONFLICT;
    }

    // SIGHASH_SINGLE must not sign an index >= output count.
    if ((sighash & 0x1f) == SIGHASH_SINGLE && index >= pstt.outputs.size()) {
        return PSTTSignResult::SIGHASH_SINGLE_OOB;
    }

    // Must compute the locktime and refuse to sign if none is valid.
    uint32_t locktime;
    if (!ComputeLocktime(pstt, locktime)) {
        return PSTTSignResult::LOCKTIME_INVALID;
    }

    // Must not add a signature whose scheme conflicts with a signature already
    // present on this input -- proactive rejection; IsSane() is the reactive/
    // structural backstop for the same rule.
    SignatureScheme incoming_scheme = sigScheme;
    for (const auto& kv : input.partial_sigs) {
        const std::vector<unsigned char>& sig = kv.second.second;
        SignatureScheme existing_scheme = (sig.size() == CPubKey::COMPACT_SIGNATURE_SIZE)
            ? SignatureScheme::SCHNORR : SignatureScheme::ECDSA;
        if (existing_scheme != incoming_scheme) {
            return PSTTSignResult::SCHEME_CONFLICT;
        }
    }

    input.FillSignatureData(sigdata);

    CMutableTransaction mtx_view = MaterializeTransaction(pstt, locktime, /*force_zero_sequence=*/false, /*use_final_scriptsig=*/false);
    MutableTransactionSignatureCreator creator(&mtx_view, index, utxo_out.nValue, sighash, sigScheme);
    ProduceSignature(provider, creator, utxo_out.scriptPubKey, sigdata);

    // pstt is taken by const reference (MaterializeTransaction needs the whole
    // PSTT, not just this input, so the signature intentionally doesn't take a
    // mutable single-input reference the way the old SignPSBTInput did) -- it's
    // the caller's responsibility to apply input.FromSignatureData(sigdata) to
    // its own mutable copy once this returns OK.
    return PSTTSignResult::OK;
}
