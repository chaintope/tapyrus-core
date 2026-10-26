// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coloridentifier.h>
#include <compat/byteswap.h>
#include <core_io.h>
#include <keystore.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <pstt.h>
#include <rpc/protocol.h>
#include <rpc/rawtransaction.h>
#include <rpc/server.h>
#include <rpc/jsonrpcrequest.h>
#include <rpc/rpctable.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/signaturecreator.h>
#include <script/signaturedata.h>
#include <script/standard.h>
#include <uint256.h>
#include <utilstrencodings.h>

#include <stdexcept>
#include <stdint.h>

#include <univalue.h>

static std::string WriteHDKeypath(std::vector<uint32_t>& keypath)
{
    std::string keypath_str = "m";
    for (uint32_t num : keypath) {
        keypath_str += "/";
        bool hardened = false;
        if (num & 0x80000000) {
            hardened = true;
            num &= ~0x80000000;
        }

        keypath_str += std::to_string(num);
        if (hardened) {
            keypath_str += "'";
        }
    }
    return keypath_str;
}

// ---------------------------------------------------------------------
// PSTT (TIP-174) -- see doc/tapyrus/pstt.md
// ---------------------------------------------------------------------

// Mirrors addTokenKV's (rpcwallet.cpp) token/amount display convention, but
// keyed off a scriptPubKey directly rather than a CTxDestination, matching
// doc/tapyrus/pstt.md's Colored Coin balance verification section: color is
// always derived from the script, never assumed from context.
static void PushTokenAmount(const CScript& script, CAmount amount, UniValue& entry)
{
    ColorIdentifier colorId = GetColorIdFromScript(script);
    entry.pushKV("token", colorId.toHexString());
    entry.pushKV("amount", (colorId.type == TokenTypes::NONE ? ValueFromAmount(amount) : amount));
}

static void PsttInputToUniv(const PSTTInput& input, UniValue& in)
{
    in.pushKV("txid", input.previous_txid.GetHex());
    in.pushKV("vout", (uint64_t)input.prevout_index);

    if (input.utxo) {
        UniValue utxo_univ(UniValue::VOBJ);
        TxToUniv(*input.utxo, uint256(), utxo_univ, false);
        in.pushKV("utxo", utxo_univ);
        if (input.prevout_index < input.utxo->vout.size()) {
            const CTxOut& out = input.utxo->vout[input.prevout_index];
            PushTokenAmount(out.scriptPubKey, out.nValue, in);
        }
    }

    if (!input.partial_sigs.empty()) {
        UniValue partial_sigs(UniValue::VOBJ);
        for (const auto& sig : input.partial_sigs) {
            partial_sigs.pushKV(HexStr(sig.second.first), HexStr(sig.second.second));
        }
        in.pushKV("partial_signatures", partial_sigs);
    }

    if (input.sighash_type) {
        in.pushKV("sighash", SighashToStr((unsigned char)*input.sighash_type));
    }

    if (!input.redeem_script.empty()) {
        UniValue r(UniValue::VOBJ);
        ScriptToUniv(input.redeem_script, r, false);
        in.pushKV("redeem_script", r);
    }

    if (!input.hd_keypaths.empty()) {
        UniValue keypaths(UniValue::VARR);
        for (auto entry : input.hd_keypaths) {
            UniValue keypath(UniValue::VOBJ);
            keypath.pushKV("pubkey", HexStr(entry.first));
            uint32_t fingerprint = entry.second.at(0);
            keypath.pushKV("master_fingerprint", strprintf("%08x", internal_bswap_32(fingerprint)));
            entry.second.erase(entry.second.begin());
            keypath.pushKV("path", WriteHDKeypath(entry.second));
            keypaths.push_back(keypath);
        }
        in.pushKV("bip32_derivs", keypaths);
    }

    if (!input.final_script_sig.empty()) {
        UniValue scriptsig(UniValue::VOBJ);
        scriptsig.pushKV("asm", ScriptToAsmStr(input.final_script_sig, true));
        scriptsig.pushKV("hex", HexStr(input.final_script_sig));
        in.pushKV("final_scriptSig", scriptsig);
    }

    if (input.sequence) in.pushKV("sequence", (uint64_t)*input.sequence);
    if (input.required_time_locktime) in.pushKV("required_time_locktime", (uint64_t)*input.required_time_locktime);
    if (input.required_height_locktime) in.pushKV("required_height_locktime", (uint64_t)*input.required_height_locktime);

    auto push_preimages = [&in](const char* name, const std::map<std::vector<unsigned char>, std::vector<unsigned char>>& map) {
        if (map.empty()) return;
        UniValue preimages(UniValue::VOBJ);
        for (const auto& entry : map) {
            preimages.pushKV(HexStr(entry.first), HexStr(entry.second));
        }
        in.pushKV(name, preimages);
    };
    push_preimages("ripemd160_preimages", input.ripemd160_preimages);
    push_preimages("sha256_preimages", input.sha256_preimages);
    push_preimages("hash160_preimages", input.hash160_preimages);
    push_preimages("hash256_preimages", input.hash256_preimages);

    if (!input.unknown.empty()) {
        UniValue unknowns(UniValue::VOBJ);
        for (auto entry : input.unknown) {
            unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
        }
        in.pushKV("unknown", unknowns);
    }
}

static void PsttOutputToUniv(const PSTTOutput& output, UniValue& out)
{
    if (output.amount) out.pushKV("amount_raw", *output.amount);
    if (!output.script.empty()) {
        UniValue s(UniValue::VOBJ);
        ScriptPubKeyToUniv(output.script, s, true);
        out.pushKV("script", s);
        if (output.amount) PushTokenAmount(output.script, *output.amount, out);
    }

    if (!output.redeem_script.empty()) {
        UniValue r(UniValue::VOBJ);
        ScriptToUniv(output.redeem_script, r, false);
        out.pushKV("redeem_script", r);
    }

    if (!output.hd_keypaths.empty()) {
        UniValue keypaths(UniValue::VARR);
        for (auto entry : output.hd_keypaths) {
            UniValue keypath(UniValue::VOBJ);
            keypath.pushKV("pubkey", HexStr(entry.first));
            uint32_t fingerprint = entry.second.at(0);
            keypath.pushKV("master_fingerprint", strprintf("%08x", internal_bswap_32(fingerprint)));
            entry.second.erase(entry.second.begin());
            keypath.pushKV("path", WriteHDKeypath(entry.second));
            keypaths.push_back(keypath);
        }
        out.pushKV("bip32_derivs", keypaths);
    }

    if (!output.unknown.empty()) {
        UniValue unknowns(UniValue::VOBJ);
        for (auto entry : output.unknown) {
            unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
        }
        out.pushKV("unknown", unknowns);
    }
}

// Parses a single {"address": amount} or {"data": hex} entry -- the same
// shape as one key-value pair of createrawtransaction's outputs array/dict --
// into a PSTTOutput. Shared by addoutputtopstt and addinputoutputpairtopstt,
// which each add exactly one output at a time rather than a whole array.
static PSTTOutput ParsePsttOutputEntry(const UniValue& output_in)
{
    if (!output_in.isObject() || output_in.size() != 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, output must be an object with exactly one key");
    }
    const std::string& name = output_in.getKeys()[0];
    const UniValue& value = output_in.getValues()[0];

    PSTTOutput out;
    if (name == "data") {
        std::vector<unsigned char> data = ParseHexV(value.getValStr(), "Data");
        out.amount = 0;
        out.script = CScript() << OP_RETURN << data;
        return out;
    }

    CTxDestination destination = DecodeDestination(name);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Tapyrus address: ") + name);
    }
    CTxOut txout = BuildDestinationTxOut(destination, value);
    out.amount = txout.nValue;
    out.script = txout.scriptPubKey;
    return out;
}

UniValue createpstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 6)
        throw std::runtime_error(
            "createpstt [{\"txid\":\"id\",\"vout\":n},...] [{\"address\":amount},{\"data\":\"hex\"},...] ( fallback_locktime ) ( inputs_modifiable ) ( outputs_modifiable ) ( has_sighash_single )\n"
            "\nCreates a bare PSTT from the given inputs and outputs (either may be empty).\n"
            "Implements the Creator role.\n"
            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",         (string, required) The malfix (malleability-fixed) transaction id of the transaction whose output is being spent\n"
            "         \"vout\":n,             (numeric, required) The index of that output within the transaction referenced by txid\n"
            "         \"sequence\":n          (numeric, optional, default=0xffffffff) The sequence number. The default disables any locktime constraint from this input; a lower value makes the PSTT's locktime binding, may also encode a BIP68 relative locktime, and can opt this transaction in to BIP125 replace-by-fee\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"               (array, required) a json array with outputs (key-value pairs)\n"
            "   [\n"
            "    {\n"
            "      \"address\": x.xxx,    (obj, optional) A key-value pair. The key (string) is a base58-encoded Tapyrus address, the value (float or string) is the amount in " + CURRENCY_UNIT + "\n"
            "    },\n"
            "    {\n"
            "      \"data\": \"hex\"        (obj, optional) A key-value pair whose key must be the literal string \"data\"; the value is arbitrary hex-encoded data embedded via OP_RETURN\n"
            "    }\n"
            "    ,...                     More key-value pairs of the above form. For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also\n"
            "                             accepted as second parameter.\n"
            "   ]\n"
            "3. fallback_locktime         (numeric, optional, default=0) PSTT_GLOBAL_FALLBACK_LOCKTIME -- the locktime used when no input constrains one\n"
            "4. inputs_modifiable         (boolean, optional, default=false) Whether further inputs may be added via addinputtopstt/addinputoutputpairtopstt\n"
            "5. outputs_modifiable        (boolean, optional, default=false) Whether further outputs may be added via addoutputtopstt/addinputoutputpairtopstt\n"
            "6. has_sighash_single        (boolean, optional, default=false) If true, marks the PSTT as using SIGHASH_SINGLE; further Constructor calls must add inputs and outputs together via addinputoutputpairtopstt instead of separate addinputtopstt/addoutputtopstt calls\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("createpstt", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"myaddress\\\":0.01}]\"")
        );

    RPCTypeCheck(request.params, {
        UniValue::VARR,
        UniValueType(), // ARR or OBJ, checked later
        UniValue::VNUM,
        UniValue::VBOOL,
        UniValue::VBOOL,
        UniValue::VBOOL,
        }, true
    );

    // Passing fallback_locktime through as ConstructTransaction's own locktime
    // param (rather than NullUniValue) is deliberate, not just convenient
    // range-check reuse: it's what makes ConstructTransaction apply its
    // existing nSequence = max-1 coupling to every input when the locktime is
    // nonzero. Without that, a nonzero fallback_locktime would be silently
    // inert once extracted -- IsFinalTx-style consensus rules only honor
    // nLockTime when at least one input carries a non-final sequence.
    // Inputs are parsed separately via ParsePsttInputEntries, not passed
    // through ConstructTransaction directly -- an empty array is passed here
    // so ConstructTransaction only handles outputs/locktime. Both use
    // createrawtransaction's txid/vout field names; ParsePsttInputEntries
    // just also accepts an optional "sequence" per entry.
    CMutableTransaction rawTx = ConstructTransaction(UniValue(UniValue::VARR), request.params[1], request.params[2], NullUniValue);
    rawTx.vin = ParsePsttInputEntries(request.params[0], rawTx.nLockTime, /*rbfOptIn=*/false);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    for (const CTxIn& txin : rawTx.vin) {
        PSTTInput input;
        input.previous_txid = txin.prevout.hashMalFix;
        input.prevout_index = txin.prevout.n;
        input.previous_txid_set = true;
        input.prevout_index_set = true;
        if (txin.nSequence != CTxIn::SEQUENCE_FINAL) input.sequence = txin.nSequence;
        pstt.inputs.push_back(std::move(input));
    }
    for (const CTxOut& txout : rawTx.vout) {
        PSTTOutput output;
        output.amount = txout.nValue;
        output.script = txout.scriptPubKey;
        pstt.outputs.push_back(std::move(output));
    }

    if (rawTx.nLockTime != 0) pstt.fallback_locktime = rawTx.nLockTime;

    bool inputs_modifiable = !request.params[3].isNull() && request.params[3].get_bool();
    bool outputs_modifiable = !request.params[4].isNull() && request.params[4].get_bool();
    bool has_sighash_single = !request.params[5].isNull() && request.params[5].get_bool();
    uint8_t modifiable = 0;
    if (inputs_modifiable) modifiable |= PSTT_TXMOD_INPUTS_MODIFIABLE;
    if (outputs_modifiable) modifiable |= PSTT_TXMOD_OUTPUTS_MODIFIABLE;
    if (has_sighash_single) modifiable |= PSTT_TXMOD_HAS_SIGHASH_SINGLE;
    if (modifiable != 0) pstt.tx_modifiable = modifiable;

    return EncodePSTT(pstt);
}

UniValue converttopstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "converttopstt \"hexstring\" ( permitsigdata inputs_modifiable outputs_modifiable )\n"
            "\nConverts a network serialized transaction to a PSTT. Implements the Creator role. Use createpstt\n"
            "or walletcreatefundedpstt for new applications instead.\n"
            "\nArguments:\n"
            "1. \"hexstring\"              (string, required) The hex string of a raw transaction\n"
            "2. permitsigdata           (boolean, optional, default=false) If true, any scriptSigs in the inputs are discarded and\n"
            "                             conversion continues. If false, RPC fails if any scriptSig is present.\n"
            "3. inputs_modifiable       (boolean, optional, default=false) Whether further inputs may be added via addinputtopstt/addinputoutputpairtopstt\n"
            "4. outputs_modifiable      (boolean, optional, default=false) Whether further outputs may be added via addoutputtopstt/addinputoutputpairtopstt\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"data\\\":\\\"00010203\\\"}]\"") +
            "\nConvert the transaction to a PSTT\n"
            + HelpExampleCli("converttopstt", "\"rawtransaction\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL, UniValue::VBOOL, UniValue::VBOOL}, true);

    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    bool permitsigdata = !request.params[1].isNull() && request.params[1].get_bool();
    for (CTxIn& input : tx.vin) {
        if (!input.scriptSig.empty() && !permitsigdata) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Inputs must not have scriptSigs");
        }
        input.scriptSig.clear();
    }

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = tx.nFeatures;
    for (const CTxIn& txin : tx.vin) {
        PSTTInput input;
        input.previous_txid = txin.prevout.hashMalFix;
        input.prevout_index = txin.prevout.n;
        input.previous_txid_set = true;
        input.prevout_index_set = true;
        if (txin.nSequence != CTxIn::SEQUENCE_FINAL) input.sequence = txin.nSequence;
        pstt.inputs.push_back(std::move(input));
    }
    for (const CTxOut& txout : tx.vout) {
        PSTTOutput output;
        output.amount = txout.nValue;
        output.script = txout.scriptPubKey;
        pstt.outputs.push_back(std::move(output));
    }
    if (tx.nLockTime != 0) pstt.fallback_locktime = tx.nLockTime;

    bool inputs_modifiable = !request.params[2].isNull() && request.params[2].get_bool();
    bool outputs_modifiable = !request.params[3].isNull() && request.params[3].get_bool();
    uint8_t modifiable = 0;
    if (inputs_modifiable) modifiable |= PSTT_TXMOD_INPUTS_MODIFIABLE;
    if (outputs_modifiable) modifiable |= PSTT_TXMOD_OUTPUTS_MODIFIABLE;
    if (modifiable != 0) pstt.tx_modifiable = modifiable;

    return EncodePSTT(pstt);
}

UniValue addinputtopstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "addinputtopstt \"pstt\" \"txid\" vout ( sequence )\n"
            "\nAppends one input to a PSTT whose Inputs-Modifiable flag is set. Implements\n"
            "the Constructor role. Refuses if the PSTT uses SIGHASH_SINGLE -- use\n"
            "addinputoutputpairtopstt instead.\n"
            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"txid\"                 (string, required) The malfix (malleability-fixed) transaction id of the transaction whose output is being spent\n"
            "3. vout                   (numeric, required) The index of that output within the transaction referenced by txid\n"
            "4. sequence               (numeric, optional, default=0xffffffff) The sequence number. The default disables any locktime constraint from this input; a lower value makes the PSTT's locktime binding, may also encode a BIP68 relative locktime, and can opt this transaction in to BIP125 replace-by-fee\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("addinputtopstt", "\"pstt\" \"mytxid\" 0")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VNUM, UniValue::VNUM}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    if (!pstt.tx_modifiable || !(*pstt.tx_modifiable & PSTT_TXMOD_INPUTS_MODIFIABLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT inputs are not modifiable");
    }
    // pstt.tx_modifiable is already known engaged by the check just above.
    if (*pstt.tx_modifiable & PSTT_TXMOD_HAS_SIGHASH_SINGLE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "PSTT has a SIGHASH_SINGLE signature; use addinputoutputpairtopstt to add an input and output together");
    }

    PSTTInput input;
    input.previous_txid = ParseHashV(request.params[1], "txid");
    int nOutput = request.params[2].get_int();
    if (nOutput < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");
    }
    input.prevout_index = (uint32_t)nOutput;
    input.previous_txid_set = true;
    input.prevout_index_set = true;
    if (!request.params[3].isNull()) {
        int64_t seq = request.params[3].get_int64();
        if (seq < 0 || seq > std::numeric_limits<uint32_t>::max()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
        }
        input.sequence = (uint32_t)seq;
    }
    pstt.inputs.push_back(std::move(input));

    uint32_t locktime;
    if (!ComputeLocktime(pstt, locktime)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Adding this input would make the PSTT's locktime unsatisfiable");
    }

    return EncodePSTT(pstt);
}

UniValue addoutputtopstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "addoutputtopstt \"pstt\" output\n"
            "\nAppends one output to a PSTT whose Outputs-Modifiable flag is set. Implements\n"
            "the Constructor role. Refuses if the PSTT uses SIGHASH_SINGLE -- use\n"
            "addinputoutputpairtopstt instead.\n"
            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"output\"               (object, required) A single key-value pair, either\n"
            "                          {\"address\": amount} or {\"data\": \"hex\"} -- same shape as\n"
            "                          one entry of createrawtransaction's outputs array\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("addoutputtopstt", "\"pstt\" \"{\\\"myaddress\\\":0.01}\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ});

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    if (!pstt.tx_modifiable || !(*pstt.tx_modifiable & PSTT_TXMOD_OUTPUTS_MODIFIABLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT outputs are not modifiable");
    }
    // pstt.tx_modifiable is already known engaged by the check just above.
    if (*pstt.tx_modifiable & PSTT_TXMOD_HAS_SIGHASH_SINGLE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "PSTT has a SIGHASH_SINGLE signature; use addinputoutputpairtopstt to add an input and output together");
    }

    pstt.outputs.push_back(ParsePsttOutputEntry(request.params[1]));
    return EncodePSTT(pstt);
}

UniValue addinputoutputpairtopstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 4 || request.params.size() > 5)
        throw std::runtime_error(
            "addinputoutputpairtopstt \"pstt\" \"txid\" vout output ( sequence )\n"
            "\nAtomically appends one input and one output to a PSTT. Implements the\n"
            "Constructor role. Required instead of separate addinputtopstt/addoutputtopstt\n"
            "calls when the PSTT uses SIGHASH_SINGLE.\n"
            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"txid\"                 (string, required) The malfix (malleability-fixed) transaction id of the transaction whose output is being spent\n"
            "3. vout                   (numeric, required) The index of that output within the transaction referenced by txid\n"
            "4. \"output\"               (object, required) A single key-value pair, either\n"
            "                          {\"address\": amount} or {\"data\": \"hex\"}\n"
            "5. sequence               (numeric, optional, default=0xffffffff) The sequence number. The default disables any locktime constraint from this input; a lower value makes the PSTT's locktime binding, may also encode a BIP68 relative locktime, and can opt this transaction in to BIP125 replace-by-fee\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("addinputoutputpairtopstt", "\"pstt\" \"mytxid\" 0 \"{\\\"myaddress\\\":0.01}\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VNUM, UniValue::VOBJ, UniValue::VNUM}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    if (!pstt.tx_modifiable || !(*pstt.tx_modifiable & PSTT_TXMOD_INPUTS_MODIFIABLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT inputs are not modifiable");
    }
    if (!pstt.tx_modifiable || !(*pstt.tx_modifiable & PSTT_TXMOD_OUTPUTS_MODIFIABLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT outputs are not modifiable");
    }

    PSTTInput input;
    input.previous_txid = ParseHashV(request.params[1], "txid");
    int nOutput = request.params[2].get_int();
    if (nOutput < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");
    }
    input.prevout_index = (uint32_t)nOutput;
    input.previous_txid_set = true;
    input.prevout_index_set = true;
    if (!request.params[4].isNull()) {
        int64_t seq = request.params[4].get_int64();
        if (seq < 0 || seq > std::numeric_limits<uint32_t>::max()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
        }
        input.sequence = (uint32_t)seq;
    }

    pstt.inputs.push_back(std::move(input));
    pstt.outputs.push_back(ParsePsttOutputEntry(request.params[3]));

    uint32_t locktime;
    if (!ComputeLocktime(pstt, locktime)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Adding this input would make the PSTT's locktime unsatisfiable");
    }

    return EncodePSTT(pstt);
}

UniValue finalizepsttconstruction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "finalizepsttconstruction \"pstt\" ( clear_inputs_modifiable clear_outputs_modifiable )\n"
            "\nDeclares a PSTT's Constructor phase finished by clearing its Inputs-Modifiable\n"
            "and/or Outputs-Modifiable flags. Implements the Constructor role.\n"
            "\nArguments:\n"
            "1. \"pstt\"                       (string, required) A base64 string of a PSTT\n"
            "2. clear_inputs_modifiable      (boolean, optional, default=true) Clear the Inputs-Modifiable flag\n"
            "3. clear_outputs_modifiable     (boolean, optional, default=true) Clear the Outputs-Modifiable flag\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("finalizepsttconstruction", "\"pstt\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL, UniValue::VBOOL}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    bool clear_inputs = request.params[1].isNull() || request.params[1].get_bool();
    bool clear_outputs = request.params[2].isNull() || request.params[2].get_bool();

    uint8_t modifiable = pstt.tx_modifiable.value_or(0);
    if (clear_inputs) modifiable &= ~PSTT_TXMOD_INPUTS_MODIFIABLE;
    if (clear_outputs) modifiable &= ~PSTT_TXMOD_OUTPUTS_MODIFIABLE;
    // Omit the field entirely rather than writing an explicit zero -- "absent"
    // and "present with value 0" both mean "not modifiable" on the wire (see
    // doc/tapyrus/pstt.md's PSTT_GLOBAL_TX_MODIFIABLE row), and createpstt/
    // converttopstt/walletcreatefundedpstt already omit it in this case.
    pstt.tx_modifiable = (modifiable != 0) ? boost::optional<uint8_t>(modifiable) : boost::none;

    return EncodePSTT(pstt);
}

// =======================================================================
// PSTT introspection helpers (decodepstt's next-role/estimated_size/
// estimated_fee fields; also used by finalizepstt below).
// =======================================================================

// The dummy provider's per-input sighash/scheme is derived from what the
// input itself already carries, not hardcoded: this makes SignPSTTInput's
// sighash-conflict and scheme-conflict pre-checks unreachable here by
// construction (they can only ever fire against a mismatch with what we
// pass in), so a dry run only ever fails for a genuine structural reason
// (missing/mismatched UTXO, bad redeem script, contradictory locktime, an
// out-of-range SIGHASH_SINGLE), never a spurious conflict against our own
// derivation.
static int FinalizeSighashFor(const PSTTInput& input)
{
    return input.sighash_type ? *input.sighash_type : SIGHASH_ALL;
}

static SignatureScheme FinalizeSigSchemeFor(const PSTTInput& input)
{
    if (input.partial_sigs.empty()) return SignatureScheme::ECDSA;
    const std::vector<unsigned char>& sig = input.partial_sigs.begin()->second.second;
    return sig.size() == CPubKey::COMPACT_SIGNATURE_SIZE ? SignatureScheme::SCHNORR : SignatureScheme::ECDSA;
}

// Dry-run of what finalizepstt would do to this one input, without mutating
// anything: true iff the input's already-collected data (partial sigs/
// redeem script) is enough to finalize right now. Reuses the same
// DUMMY_SIGNING_PROVIDER technique finalizepstt itself uses (a keyless
// provider can't add anything new, so ProduceSignature can only ever report
// "complete" here if what's already in the PSTT already satisfies the
// script).
static bool CanFinalizeInputNow(const PartiallySignedTapyrusTransaction& pstt, unsigned int i)
{
    const PSTTInput& input = pstt.inputs.at(i);
    if (!input.final_script_sig.empty()) return true;
    if (!input.utxo) return false;

    SignatureData sigdata;
    PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, i, sigdata,
                                           FinalizeSighashFor(input), FinalizeSigSchemeFor(input));
    return result == PSTTSignResult::OK && sigdata.complete;
}

// Every role that could act on this PSTT right now, in the same rough
// ladder as Bitcoin Core's analyzepsbt: constructor (inputs/outputs still
// modifiable), updater (some input has no PSTT_IN_UTXO yet), signer (some
// input has a UTXO but its collected data isn't enough to finalize),
// finalizer (some input could finalize now but hasn't), extractor (every
// input already carries PSTT_IN_FINAL_SCRIPTSIG, or there are no inputs).
// These are independent, not mutually exclusive -- e.g. a freshly
// wallet-funded, still-modifiable, unsigned PSTT is both "constructor" (more
// inputs/outputs could still be added) and "signer" (its attached UTXOs
// already make it signable); the caller decides whether to sign now or wait
// for construction to close. Returned in this canonical ladder order,
// filtered to only the roles that currently apply.
static std::vector<std::string> PsttNextRoles(const PartiallySignedTapyrusTransaction& pstt)
{
    std::vector<std::string> roles;
    if (pstt.tx_modifiable && (*pstt.tx_modifiable & (PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE)) != 0) {
        roles.push_back("constructor");
    }

    bool any_missing_utxo = false;
    bool any_needs_signer = false;
    bool any_ready_to_finalize = false;
    for (unsigned int i = 0; i < pstt.inputs.size(); ++i) {
        const PSTTInput& input = pstt.inputs.at(i);
        if (!input.final_script_sig.empty()) continue;
        if (!input.utxo) {
            any_missing_utxo = true;
            continue;
        }
        if (CanFinalizeInputNow(pstt, i)) {
            any_ready_to_finalize = true;
        } else {
            any_needs_signer = true;
        }
    }

    if (any_missing_utxo) roles.push_back("updater");
    if (any_needs_signer) roles.push_back("signer");
    if (any_ready_to_finalize) roles.push_back("finalizer");
    if (!any_missing_utxo && !any_needs_signer && !any_ready_to_finalize) roles.push_back("extractor");
    return roles;
}

static bool AllInputsHaveUtxo(const PartiallySignedTapyrusTransaction& pstt)
{
    for (const PSTTInput& input : pstt.inputs) {
        if (!input.utxo || !input.prevout_index_set || input.prevout_index >= input.utxo->vout.size()) return false;
    }
    return true;
}

// The scriptSig this input would contribute: the real
// PSTT_IN_FINAL_SCRIPTSIG if already finalized, otherwise a same-size dummy
// completion of whatever's already known about it (redeem script, any
// already-collected pubkeys), via a throwaway keystore that holds no real
// private keys -- DUMMY_SIGNATURE_CREATOR fabricates a plausibly-sized fake
// signature for every public key the keystore claims to know, the same
// technique CWallet::CalculateMaximumSignedTxSize uses for fee estimation,
// just without needing a wallet. Purely a local size probe; never mutates
// the real PSTT. Caller must have already confirmed input.utxo is set.
static CScript EstimateInputScriptSig(const PartiallySignedTapyrusTransaction& pstt, unsigned int i)
{
    const PSTTInput& input = pstt.inputs.at(i);
    if (!input.final_script_sig.empty()) return input.final_script_sig;

    CBasicKeyStore keystore;
    if (!input.redeem_script.empty()) keystore.AddCScript(input.redeem_script);
    for (const auto& kv : input.hd_keypaths) keystore.AddWatchOnly(GetScriptForRawPubKey(kv.first));
    for (const auto& kv : input.partial_sigs) keystore.AddWatchOnly(GetScriptForRawPubKey(kv.second.first));

    SignatureData sigdata;
    input.FillSignatureData(sigdata);
    const CTxOut& utxo_out = input.utxo->vout.at(input.prevout_index);
    ProduceSignature(keystore, DUMMY_SIGNATURE_CREATOR, utxo_out.scriptPubKey, sigdata);
    return sigdata.scriptSig;
}

// Estimated size in bytes of the transaction this PSTT would extract to,
// using EstimateInputScriptSig for any input not yet finalized. Caller must
// have already confirmed every input has a usable, in-range UTXO attached
// (AllInputsHaveUtxo) -- without that there's no scriptPubKey to size a
// dummy completion against for at least one input, so no estimate is
// possible at all. Fidelity depends on what key material happens to be
// attached: with BIP32 derivation paths or partial sigs already collected,
// the dummy completion approximates a real signature's size well; with
// none (e.g. a createpstt+updatepstt PSTT built without a wallet), the
// keystore stays empty and this is essentially the unsigned size.
static int64_t EstimatePsttSize(const PartiallySignedTapyrusTransaction& pstt)
{
    uint32_t locktime = 0;
    ComputeLocktime(pstt, locktime); // best-effort; 0 if inconsistent -- size estimate only, not used for anything consensus-relevant

    CMutableTransaction mtx;
    mtx.nFeatures = pstt.tx_features.value_or(CTransaction::CURRENT_FEATURES);
    mtx.nLockTime = locktime;
    for (unsigned int i = 0; i < pstt.inputs.size(); ++i) {
        const PSTTInput& input = pstt.inputs.at(i);
        CTxIn txin(COutPoint(input.previous_txid, input.prevout_index), EstimateInputScriptSig(pstt, i),
                   input.sequence.value_or(CTxIn::SEQUENCE_FINAL));
        mtx.vin.push_back(std::move(txin));
    }
    for (const PSTTOutput& output : pstt.outputs) {
        mtx.vout.emplace_back(output.amount.value_or(0), output.script);
    }
    return ::GetSerializeSize(CTransaction(mtx), SER_NETWORK, PROTOCOL_VERSION);
}

// Net TPC moved by this PSTT (inputs minus outputs) -- the fee, since fees
// are payable only in TPC (doc/tapyrus/pstt.md, Fee Provider workflow) and
// every Colored Coin transfer nets to zero for its own color by consensus
// rule. Caller must have already confirmed every input has a usable,
// in-range UTXO attached.
static CAmount EstimatePsttFee(const PartiallySignedTapyrusTransaction& pstt)
{
    CAmount in_tpc = 0;
    for (const PSTTInput& input : pstt.inputs) {
        const CTxOut& utxo_out = input.utxo->vout.at(input.prevout_index);
        if (GetColorIdFromScript(utxo_out.scriptPubKey).type == TokenTypes::NONE) in_tpc += utxo_out.nValue;
    }
    CAmount out_tpc = 0;
    for (const PSTTOutput& output : pstt.outputs) {
        if (GetColorIdFromScript(output.script).type == TokenTypes::NONE) out_tpc += output.amount.value_or(0);
    }
    return in_tpc - out_tpc;
}

UniValue updatepstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "updatepstt \"pstt\" [\"hexstring\",...]\n"
            "\nAttaches PSTT_IN_UTXO to any input that doesn't already have one, by\n"
            "matching each supplied raw transaction's malfix txid against the PSTT's own\n"
            "input previous_txids. Implements the Updater role without needing a loaded\n"
            "wallet -- unlike walletupdatepstt, the previous transactions come from the\n"
            "caller, not mapWallet, and no redeem script / BIP32 derivation discovery is\n"
            "attempted (there is no wallet here to discover them from). Never adds,\n"
            "removes, or reorders inputs/outputs -- only ever attaches PSTT_IN_UTXO to\n"
            "what's already there. A supplied transaction whose txid doesn't match any\n"
            "input is silently ignored, not an error -- the caller may not know in\n"
            "advance which of several candidates a given PSTT actually needs.\n"

            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"txs\"                  (array, required) A json array of hex-encoded raw transactions\n"
            "    [\n"
            "      \"hexstring\"        (string) A previous transaction this PSTT may spend from\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "{\n"
            "  \"pstt\" : \"value\",                (string)  The base64-encoded partially signed transaction\n"
            "  \"all_utxos_attached\" : true|false, (boolean) Whether every input now has a PSTT_IN_UTXO\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("updatepstt", "\"pstt\" \"[\\\"myhex1\\\",\\\"myhex2\\\"]\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR});

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    UniValue txs = request.params[1].get_array();
    std::map<uint256, CTransactionRef> known_txs;
    for (unsigned int idx = 0; idx < txs.size(); ++idx) {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, txs[idx].get_str())) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed for tx %d", idx));
        }
        CTransactionRef txref = MakeTransactionRef(std::move(mtx));
        known_txs[txref->GetHashMalFix()] = txref;
    }

    for (PSTTInput& input : pstt.inputs) {
        if (input.utxo || !input.previous_txid_set) continue; // already attached, or this input isn't even valid yet
        auto it = known_txs.find(input.previous_txid);
        if (it == known_txs.end()) continue; // not among the supplied txs -- leave for a later Updater round
        if (input.prevout_index_set && input.prevout_index >= it->second->vout.size()) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Input prevout index out of range");
        }
        input.utxo = it->second;
    }

    bool all_attached = true;
    for (const PSTTInput& input : pstt.inputs) {
        if (!input.utxo) {
            all_attached = false;
            break;
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("pstt", EncodePSTT(pstt));
    result.pushKV("all_utxos_attached", all_attached);
    return result;
}

UniValue decodepstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodepstt \"pstt\"\n"
            "\nReturn a JSON object representing the serialized, base64-encoded "
            "Partially Signed Tapyrus Transaction (see doc/tapyrus/pstt.md).\n"
            "\nAlso reports \"next\" (a JSON array of every role that could act on this PSTT\n"
            "right now, in ladder order: constructor, updater, signer, finalizer,\n"
            "extractor -- these are independent, not mutually exclusive, e.g. a\n"
            "still-modifiable but already-fully-funded PSTT reports both \"constructor\"\n"
            "and \"signer\") and, once every input has a UTXO attached, \"estimated_size\"\n"
            "for the transaction this PSTT would currently extract to -- an estimate only,\n"
            "using whatever BIP32 derivation/partial-signature data happens to be attached\n"
            "right now to size a dummy completion of each unfinalized input (with none\n"
            "attached, e.g. a createpstt+updatepstt PSTT built without a wallet, this is\n"
            "essentially the unsigned size), and can still change while \"next\" contains\n"
            "\"constructor\" or \"signer\". \"estimated_fee\" (inputs minus outputs, in TPC)\n"
            "is included alongside it unless it would be negative, which just means this\n"
            "PSTT is still gathering inputs.\n"

            "\nArguments:\n"
            "1. \"pstt\"            (string, required) The PSTT base64 string\n"

            "\nExamples:\n"
            + HelpExampleCli("decodepstt", "\"pstt\"")
    );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    UniValue result(UniValue::VOBJ);

    if (pstt.tx_features) result.pushKV("tx_features", *pstt.tx_features);
    if (pstt.fallback_locktime) result.pushKV("fallback_locktime", (uint64_t)*pstt.fallback_locktime);
    {
        // Report the effective bitfield even when the field is absent from
        // the wire -- absent and present-with-value-0 both mean "not
        // modifiable" (see doc/tapyrus/pstt.md's PSTT_GLOBAL_TX_MODIFIABLE
        // row), so decodepstt's diagnostic output shouldn't vary with which
        // of those two equivalent encodings a given PSTT happens to use.
        uint8_t modifiable = pstt.tx_modifiable.value_or(0);
        UniValue mod(UniValue::VOBJ);
        mod.pushKV("inputs_modifiable", bool(modifiable & PSTT_TXMOD_INPUTS_MODIFIABLE));
        mod.pushKV("outputs_modifiable", bool(modifiable & PSTT_TXMOD_OUTPUTS_MODIFIABLE));
        mod.pushKV("has_sighash_single", bool(modifiable & PSTT_TXMOD_HAS_SIGHASH_SINGLE));
        result.pushKV("tx_modifiable", mod);
    }
    if (pstt.version) result.pushKV("version", (uint64_t)*pstt.version);

    if (!pstt.xpubs.empty()) {
        UniValue xpubs(UniValue::VARR);
        for (const auto& entry : pstt.xpubs) {
            UniValue x(UniValue::VOBJ);
            std::vector<unsigned char> keydata(BIP32_EXTKEY_WITH_VERSION_SIZE);
            entry.first.EncodeWithVersion(keydata.data());
            x.pushKV("xpub", HexStr(keydata));
            std::vector<uint32_t> path = entry.second;
            if (!path.empty()) {
                uint32_t fingerprint = path.at(0);
                x.pushKV("master_fingerprint", strprintf("%08x", internal_bswap_32(fingerprint)));
                path.erase(path.begin());
                x.pushKV("path", WriteHDKeypath(path));
            }
            xpubs.push_back(x);
        }
        result.pushKV("xpubs", xpubs);
    }

    uint32_t locktime;
    if (ComputeLocktime(pstt, locktime)) {
        result.pushKV("locktime", (uint64_t)locktime);
    }
    try {
        result.pushKV("identification_txid", pstt.GetIdentifier().GetHex());
    } catch (const std::exception&) {
        // No valid locktime yet (contradictory required locktimes) -- the
        // identifier can't be computed; omit rather than error, since
        // decodepstt is an inspection tool and this is exactly the kind of
        // malformed-but-parseable PSTT it should still be able to show.
    }

    if (!pstt.unknown.empty()) {
        UniValue unknowns(UniValue::VOBJ);
        for (auto entry : pstt.unknown) {
            unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
        }
        result.pushKV("unknown", unknowns);
    }

    UniValue inputs(UniValue::VARR);
    for (const PSTTInput& input : pstt.inputs) {
        UniValue in(UniValue::VOBJ);
        PsttInputToUniv(input, in);
        inputs.push_back(in);
    }
    result.pushKV("inputs", inputs);

    UniValue outputs(UniValue::VARR);
    for (const PSTTOutput& output : pstt.outputs) {
        UniValue out(UniValue::VOBJ);
        PsttOutputToUniv(output, out);
        outputs.push_back(out);
    }
    result.pushKV("outputs", outputs);

    UniValue next(UniValue::VARR);
    for (const std::string& role : PsttNextRoles(pstt)) next.push_back(role);
    result.pushKV("next", next);
    if (AllInputsHaveUtxo(pstt)) {
        result.pushKV("estimated_size", EstimatePsttSize(pstt));
        CAmount fee = EstimatePsttFee(pstt);
        if (fee >= 0) result.pushKV("estimated_fee", ValueFromAmount(fee)); // negative just means still gathering inputs
    }

    return result;
}

UniValue combinepstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "combinepstt [\"pstt\",...]\n"
            "\nCombine multiple partially signed Tapyrus transactions that all refer to\n"
            "the same underlying transaction into one. Implements the Combiner role.\n"

            "\nArguments:\n"
            "1. \"txs\"                   (string) A json array of base64 strings of partially signed transactions\n"
            "    [\n"
            "      \"pstt\"             (string) A base64 string of a PSTT\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "  \"pstt\"          (string) The base64-encoded combined partially signed transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("combinepstt", "[\"mybase64_1\", \"mybase64_2\", \"mybase64_3\"]")
        );

    RPCTypeCheck(request.params, {UniValue::VARR}, true);

    std::vector<PartiallySignedTapyrusTransaction> psttxs;
    UniValue txs = request.params[0].get_array();
    for (unsigned int i = 0; i < txs.size(); ++i) {
        PartiallySignedTapyrusTransaction pstt;
        std::string error;
        if (!DecodePSTT(pstt, txs[i].get_str(), error)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
        }
        psttxs.push_back(pstt);
    }

    if (psttxs.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Parameter 'txs' cannot be empty");
    }

    PartiallySignedTapyrusTransaction merged_pstt(psttxs[0]);
    for (auto it = std::next(psttxs.begin()); it != psttxs.end(); ++it) {
        if (!merged_pstt.HasSameIdentifierAs(*it)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTTs do not refer to the same transaction.");
        }
        try {
            merged_pstt.Merge(*it);
        } catch (const std::exception& e) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Cannot combine PSTTs: %s", e.what()));
        }
    }
    if (!merged_pstt.IsSane()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Merged PSTT is inconsistent");
    }

    return EncodePSTT(merged_pstt);
}

UniValue joinpstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "joinpstt [\"pstt\",...]\n"
            "\nJoin multiple distinct, still-under-construction PSTTs -- each with its own\n"
            "inputs and outputs -- into one larger PSTT, by concatenating their inputs and\n"
            "outputs in the order supplied. Lets a large PSTT be built up as several\n"
            "independently-constructed pieces (e.g. contributed by different parties, or\n"
            "built in parallel) and then combined, rather than requiring every input/output\n"
            "to be added one at a time to a single PSTT via addinputtopstt/addoutputtopstt.\n"
            "Implements the Constructor role. This is not the Combiner (see combinepstt):\n"
            "combinepstt merges alternate signing progress on the *same* underlying\n"
            "transaction, while joinpstt assembles *different* inputs/outputs from\n"
            "multiple PSTTs into a new, larger transaction.\n"
            "\nEvery supplied PSTT must still have both its Inputs-Modifiable and\n"
            "Outputs-Modifiable flags set (call finalizepsttconstruction only after\n"
            "joining, never before), must not have its Has-SIGHASH_SINGLE flag set, and\n"
            "none of its inputs may carry a partial signature -- a SIGHASH_SINGLE\n"
            "signature (partial or final) commits to its own input's position matching a\n"
            "specific output position, an invariant joining would silently break by\n"
            "shifting every position after the first PSTT. Join first, add any\n"
            "SIGHASH_SINGLE input/output pair afterwards. The same previous output may not\n"
            "be spent by more than one of the supplied PSTTs.\n"
            "\nfallback_locktime becomes the maximum of the joined PSTTs' fallback_locktime\n"
            "values. PSTT_GLOBAL_VERSION is not carried over from any input PSTT.\n"

            "\nArguments:\n"
            "1. \"txs\"                   (string) A json array of base64 strings of PSTTs to join\n"
            "    [\n"
            "      \"pstt\"             (string) A base64 string of a PSTT\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "  \"pstt\"          (string) The base64-encoded joined partially signed transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("joinpstt", "[\"mybase64_1\", \"mybase64_2\", \"mybase64_3\"]")
        );

    RPCTypeCheck(request.params, {UniValue::VARR}, true);

    UniValue txs = request.params[0].get_array();
    if (txs.size() < 2) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "joinpstt requires at least two PSTTs");
    }

    std::vector<PartiallySignedTapyrusTransaction> psttxs;
    for (unsigned int i = 0; i < txs.size(); ++i) {
        PartiallySignedTapyrusTransaction pstt;
        std::string error;
        if (!DecodePSTT(pstt, txs[i].get_str(), error)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
        }
        psttxs.push_back(std::move(pstt));
    }

    PartiallySignedTapyrusTransaction joined;
    joined.tx_features = CTransaction::CURRENT_FEATURES;
    std::set<COutPoint> seen_outpoints;
    std::set<std::vector<unsigned char>> seen_xpubs;
    for (unsigned int i = 0; i < psttxs.size(); ++i) {
        const PartiallySignedTapyrusTransaction& pstt = psttxs[i];
        uint8_t modifiable = pstt.tx_modifiable.value_or(0);
        if (!(modifiable & PSTT_TXMOD_INPUTS_MODIFIABLE) || !(modifiable & PSTT_TXMOD_OUTPUTS_MODIFIABLE)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("PSTT %d is not still under construction (Inputs/Outputs-Modifiable must both be set)", i));
        }
        if (modifiable & PSTT_TXMOD_HAS_SIGHASH_SINGLE) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("PSTT %d has its Has-SIGHASH_SINGLE flag set; join it after joinpstt instead", i));
        }
        for (unsigned int j = 0; j < pstt.inputs.size(); ++j) {
            const PSTTInput& input = pstt.inputs[j];
            if (!input.partial_sigs.empty() || !input.final_script_sig.empty()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("PSTT %d input %d is already signed; only unsigned PSTTs may be joined", i, j));
            }
            if (input.previous_txid_set && input.prevout_index_set) {
                if (!seen_outpoints.insert(COutPoint(input.previous_txid, input.prevout_index)).second) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("PSTT %d input %d spends an outpoint already spent by another joined PSTT", i, j));
                }
            }
        }

        if (pstt.tx_features && *pstt.tx_features > *joined.tx_features) joined.tx_features = pstt.tx_features;
        if (pstt.fallback_locktime && (!joined.fallback_locktime || *pstt.fallback_locktime > *joined.fallback_locktime)) {
            joined.fallback_locktime = pstt.fallback_locktime;
        }
        for (const auto& entry : pstt.xpubs) {
            if (seen_xpubs.insert(XpubEntryCanonicalBytes(entry)).second) {
                joined.xpubs.push_back(entry);
            }
        }
        for (const auto& kv : pstt.unknown) joined.unknown.insert(kv); // pick-first tier, same as combinepstt's Merge()

        joined.inputs.insert(joined.inputs.end(), pstt.inputs.begin(), pstt.inputs.end());
        joined.outputs.insert(joined.outputs.end(), pstt.outputs.begin(), pstt.outputs.end());
    }
    joined.tx_modifiable = PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE;

    uint32_t locktime;
    if (!ComputeLocktime(joined, locktime)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Joining these PSTTs would make the resulting locktime unsatisfiable");
    }
    if (!joined.IsSane()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Joined PSTT is inconsistent");
    }

    return EncodePSTT(joined);
}

UniValue finalizepstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "finalizepstt \"pstt\" ( extract )\n"
            "\nFinalize the inputs of a PSTT. If complete, extracts a broadcastable\n"
            "transaction; otherwise returns a partially finalized PSTT. Implements the\n"
            "Finalizer and (optionally) Extractor roles.\n"

            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"extract\"              (boolean, optional, default=true) If true and the transaction is complete,\n"
            "                             extract and return the complete transaction in normal network serialization\n"
            "                             instead of the PSTT.\n"

            "\nResult:\n"
            "{\n"
            "  \"pstt\" : \"value\",          (string) The base64-encoded partially signed transaction if not extracted\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded network transaction if extracted\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("finalizepstt", "\"pstt\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    if (pstt.tx_modifiable && (*pstt.tx_modifiable & (PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE)) != 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT construction is not finished (inputs and/or outputs are still modifiable)");
    }

    bool complete = true;
    for (unsigned int i = 0; i < pstt.inputs.size(); ++i) {
        PSTTInput& input = pstt.inputs.at(i);
        if (!input.final_script_sig.empty()) continue; // already finalized

        SignatureData sigdata;
        PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, i, sigdata,
                                               FinalizeSighashFor(input), FinalizeSigSchemeFor(input));
        if (result == PSTTSignResult::MISSING_UTXO) {
            complete = false;
            continue;
        }
        if (result != PSTTSignResult::OK) {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, strprintf("Finalizing input %d failed: %s", i, PSTTSignResultToString(result)));
        }

        input.FromSignatureData(sigdata);
        if (!sigdata.complete) {
            complete = false;
            continue;
        }

        // Finalizer field policy: keep required fields, PSTT_IN_UTXO and
        // unknowns; strip everything only useful during signing.
        input.partial_sigs.clear();
        input.sighash_type = boost::none;
        input.redeem_script.clear();
        input.hd_keypaths.clear();
        input.ripemd160_preimages.clear();
        input.sha256_preimages.clear();
        input.hash160_preimages.clear();
        input.hash256_preimages.clear();
    }

    UniValue result(UniValue::VOBJ);
    bool extract = request.params[1].isNull() || request.params[1].get_bool();
    if (complete && extract) {
        CMutableTransaction mtx = ExtractPSTT(pstt);
        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << mtx;
        result.pushKV("hex", HexStr(ssTx.begin(), ssTx.end()));
    } else {
        result.pushKV("pstt", EncodePSTT(pstt));
    }
    result.pushKV("complete", complete);
    return result;
}

UniValue extractpstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "extractpstt \"pstt\"\n"
            "\nExtract the fully signed transaction from a PSTT, ready for sendrawtransaction.\n"
            "Implements the Extractor role. Throws if any input isn't finalized.\n"

            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"

            "\nResult:\n"
            "  \"hex\"              (string) The hex-encoded network transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("extractpstt", "\"pstt\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    CMutableTransaction mtx;
    try {
        mtx = ExtractPSTT(pstt);
    } catch (const std::runtime_error& e) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, e.what());
    }

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << mtx;
    return HexStr(ssTx.begin(), ssTx.end());
}

UniValue signpsttwithkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "signpsttwithkey \"pstt\" [\"privatekey1\",...] ( \"sighashtype\" \"sigscheme\" )\n"
            "\nSign inputs of a PSTT with the given private keys (Signer role, no wallet required).\n"

            "\nArguments:\n"
            "1. \"pstt\"                          (string, required) The PSTT base64 string\n"
            "2. \"privkeys\"                      (string, required) A json array of private keys for signing\n"
            "    [\n"
            "      \"privatekey\"                  (string) private key in WIF (Wallet Import Format, see dumpprivkey)\n"
            "      ,...\n"
            "    ]\n"
            "3. \"sighashtype\"                    (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"
            "4. \"sigscheme\"                    (string, optional, default=ECDSA) The signature scheme to use\n"
            "       \"ECDSA\"\n"
            "       \"SCHNORR\"\n"

            "\nResult:\n"
            "{\n"
            "  \"pstt\" : \"value\",          (string) The base64-encoded partially signed transaction\n"
            "  \"complete\" : true|false,   (boolean) If every input with a UTXO now has a complete signature\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signpsttwithkey", "\"mypstt\" \"[\\\"key1\\\",\\\"key2\\\"]\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VSTR, UniValue::VSTR}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    FlatSigningProvider provider;
    const UniValue& keys = request.params[1].get_array();
    for (unsigned int idx = 0; idx < keys.size(); ++idx) {
        UniValue k = keys[idx];
        CKey key = DecodeSecret(k.get_str());
        if (!key.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
        }
        CPubKey pubkey = key.GetPubKey();
        provider.keys[pubkey.GetID()] = key;
        provider.pubkeys[pubkey.GetID()] = pubkey;
    }
    // Redeem scripts already attached to inputs by an earlier Updater step
    // are made available for P2SH/CP2SH resolution.
    for (const PSTTInput& in : pstt.inputs) {
        if (!in.redeem_script.empty()) {
            provider.scripts[CScriptID(in.redeem_script)] = in.redeem_script;
        }
    }

    int sighash = ParseSighashString(request.params[2]);
    SignatureScheme sigScheme = ParseSigSchemeString(request.params[3]);

    bool complete = true;
    for (unsigned int i = 0; i < pstt.inputs.size(); ++i) {
        PSTTInput& input = pstt.inputs.at(i);
        if (!input.final_script_sig.empty()) continue; // already finalized
        SignatureData sigdata;
        PSTTSignResult result = SignPSTTInput(provider, pstt, i, sigdata, sighash, sigScheme);
        if (result == PSTTSignResult::MISSING_UTXO) {
            // Not this call's job to complain -- an incremental multi-party
            // PSTT may genuinely have inputs no one has attached a UTXO to
            // yet; leave it for a later Updater/Signer round.
            complete = false;
            continue;
        }
        if (result != PSTTSignResult::OK) {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, strprintf("Signing input %d failed: %s", i, PSTTSignResultToString(result)));
        }
        input.FromSignatureData(sigdata);
        if (!sigdata.complete) complete = false;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("pstt", EncodePSTT(pstt));
    result.pushKV("complete", complete);
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                            actor (function)            argNames
  //  --------------------- ------------------------        -----------------------     ----------
    { "rawtransactions",    "createpstt",                   &createpstt,                {"inputs","outputs","fallback_locktime","inputs_modifiable","outputs_modifiable","has_sighash_single"} },
    { "rawtransactions",    "converttopstt",                &converttopstt,             {"hexstring","permitsigdata","inputs_modifiable","outputs_modifiable"} },
    { "rawtransactions",    "addinputtopstt",               &addinputtopstt,            {"pstt","txid","vout","sequence"} },
    { "rawtransactions",    "addoutputtopstt",              &addoutputtopstt,           {"pstt","output"} },
    { "rawtransactions",    "addinputoutputpairtopstt",     &addinputoutputpairtopstt,  {"pstt","txid","vout","output","sequence"} },
    { "rawtransactions",    "finalizepsttconstruction",     &finalizepsttconstruction,  {"pstt","clear_inputs_modifiable","clear_outputs_modifiable"} },
    { "rawtransactions",    "updatepstt",                   &updatepstt,                {"pstt","txs"} },
    { "rawtransactions",    "decodepstt",                   &decodepstt,                {"pstt"} },
    { "rawtransactions",    "combinepstt",                  &combinepstt,               {"txs"} },
    { "rawtransactions",    "joinpstt",                     &joinpstt,                  {"txs"} },
    { "rawtransactions",    "finalizepstt",                 &finalizepstt,              {"pstt", "extract"} },
    { "rawtransactions",    "extractpstt",                  &extractpstt,               {"pstt"} },
    { "rawtransactions",    "signpsttwithkey",               &signpsttwithkey,           {"pstt","privkeys","sighashtype","sigscheme"} },
};

void RegisterPSTTRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
