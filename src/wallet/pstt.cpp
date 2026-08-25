// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/pstt.h>

#include <coloridentifier.h>
#include <rpc/protocol.h>
#include <script/sign.h>
#include <wallet/wallet.h>

bool FillPSTT(const CWallet* pwallet, PartiallySignedTapyrusTransaction& pstt, int sighash_type, bool sign,
              bool bip32derivs, SignatureScheme sigScheme)
{
    LOCK(pwallet->cs_wallet);
    bool complete = true;
    bool signed_any = false;

    for (unsigned int i = 0; i < pstt.inputs.size(); ++i) {
        PSTTInput& input = pstt.inputs.at(i);

        // Updater: attach the UTXO from the wallet if we don't already have
        // one. If we don't know about this input, skip it and let someone
        // else deal with it -- the Updater must not add/remove/alter inputs,
        // only fill in fields on what's already there.
        if (!input.utxo && input.previous_txid_set) {
            const auto it = pwallet->mapWallet.find(input.previous_txid);
            if (it != pwallet->mapWallet.end()) {
                const CWalletTx& wtx = it->second;
                if (input.prev_out_index_set && input.prev_out_index >= wtx.tx->vout.size()) {
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Input prevout index out of range");
                }
                input.utxo = wtx.tx;
            }
        }

        if (sign && input.sighash_type && *input.sighash_type != sighash_type) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Specified Sighash and sighash in PSTT do not match.");
        }

        if (!input.final_script_sig.empty()) {
            continue; // already finalized, nothing to update or sign
        }

        SignatureData sigdata;
        PSTTSignResult result = sign
            ? SignPSTTInput(*pwallet, pstt, i, sigdata, sighash_type, sigScheme)
            : SignPSTTInput(PublicOnlySigningProvider(pwallet), pstt, i, sigdata, sighash_type, sigScheme);

        if (result == PSTTSignResult::MISSING_UTXO) {
            // Not this call's problem to solve -- an incremental multi-party
            // PSTT may genuinely have inputs no one has attached a UTXO to
            // yet; leave it for a later Updater/Signer round.
            complete = false;
        } else if (result != PSTTSignResult::OK) {
            throw JSONRPCError(RPC_TRANSACTION_ERROR,
                strprintf("Filling/signing input %d failed: %s", i, PSTTSignResultToString(result)));
        } else {
            // sign=true only means signing was attempted -- SignPSTTInput
            // returns OK even when the wallet has no key for this input and
            // contributed nothing (e.g. a foreign input in an incremental
            // multi-party PSTT, per walletprocesspstt's own contract of
            // being a harmless no-op for inputs it can't sign). The post-sign
            // PSTT_GLOBAL_TX_MODIFIABLE mutation must only fire on an input
            // this call actually added a signature to, so compare
            // partial_sigs/final_script_sig before and after.
            size_t partial_sigs_before = input.partial_sigs.size();
            bool finalized_before = !input.final_script_sig.empty();
            input.FromSignatureData(sigdata);
            if (!sigdata.complete) complete = false;
            bool contributed = (!finalized_before && !input.final_script_sig.empty())
                || input.partial_sigs.size() > partial_sigs_before;
            if (sign && contributed) signed_any = true;
        }

        if (bip32derivs) {
            for (const auto& pubkey_it : sigdata.misc_pubkeys) {
                AddKeypathToMap(pwallet, pubkey_it.first, input.hd_keypaths);
            }
        }
    }

    // Fill in BIP32 keypaths for outputs so hardware wallets/watch-only
    // signers can identify change. Outputs don't need signing, so this uses
    // a throwaway dummy transaction purely to reuse ProduceSignature's
    // pubkey-discovery machinery -- PSTT has no embedded global tx to
    // piggyback on the way the old PSBT code's FillPSBT did, so the dummy is
    // built explicitly here instead.
    if (bip32derivs) {
        CMutableTransaction dummy_tx;
        dummy_tx.vin.push_back(CTxIn());
        for (unsigned int i = 0; i < pstt.outputs.size(); ++i) {
            const PSTTOutput& output = pstt.outputs.at(i);
            if (!output.amount || output.script.empty()) continue;

            SignatureData sigdata;
            output.FillSignatureData(sigdata);
            MutableTransactionSignatureCreator creator(&dummy_tx, 0, *output.amount, SIGHASH_ALL);
            ProduceSignature(*pwallet, creator, output.script, sigdata);

            for (const auto& pubkey_it : sigdata.misc_pubkeys) {
                AddKeypathToMap(pwallet, pubkey_it.first, pstt.outputs.at(i).hd_keypaths);
            }
        }
    }

    if (sign && signed_any) {
        ApplyPsttPostSignModifiableRules(pstt, sighash_type);
    }

    return complete;
}

void ApplyPsttPostSignModifiableRules(PartiallySignedTapyrusTransaction& pstt, int sighash_type)
{
    int base_sighash = sighash_type & 0x1f;
    bool anyonecanpay = (sighash_type & SIGHASH_ANYONECANPAY) != 0;
    uint8_t modifiable = pstt.tx_modifiable.value_or(0);
    if (!anyonecanpay) modifiable &= ~PSTT_TXMOD_INPUTS_MODIFIABLE;
    if (base_sighash != SIGHASH_NONE && base_sighash != SIGHASH_SINGLE) modifiable &= ~PSTT_TXMOD_OUTPUTS_MODIFIABLE;
    if (base_sighash == SIGHASH_SINGLE) modifiable |= PSTT_TXMOD_HAS_SIGHASH_SINGLE;
    // Omit the field entirely rather than writing an explicit zero -- "absent"
    // and "present with value 0" both mean "not modifiable" on the wire (see
    // doc/tapyrus/pstt.md's PSTT_GLOBAL_TX_MODIFIABLE row), matching
    // createpstt/converttopstt/walletcreatefundedpstt's own convention.
    pstt.tx_modifiable = (modifiable != 0) ? boost::optional<uint8_t>(modifiable) : boost::none;
}

void ComputePsttColorBalances(const PartiallySignedTapyrusTransaction& pstt,
                               TxColoredCoinBalancesMap& in, TxColoredCoinBalancesMap& out)
{
    for (const PSTTInput& input : pstt.inputs) {
        if (!input.utxo || !input.prev_out_index_set || input.prev_out_index >= input.utxo->vout.size()) {
            continue; // color/amount not knowable without an attached UTXO
        }
        const CTxOut& utxo_out = input.utxo->vout[input.prev_out_index];
        ColorIdentifier colorId = GetColorIdFromScript(utxo_out.scriptPubKey);
        in[colorId] += utxo_out.nValue;
    }
    for (const PSTTOutput& output : pstt.outputs) {
        if (!output.amount || output.script.empty()) continue;
        ColorIdentifier colorId = GetColorIdFromScript(output.script);
        out[colorId] += *output.amount;
    }
}
