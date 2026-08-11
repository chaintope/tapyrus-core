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

        if (sign && input.sighash_type > 0 && input.sighash_type != sighash_type) {
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
            input.FromSignatureData(sigdata);
            if (!sigdata.complete) complete = false;
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
    // pubkey-discovery machinery (mirrors FillPSBT's own documented
    // approach in rpcwallet.cpp: "Dummy tx so we can use ProduceSignature to
    // get stuff out" -- there, a real embedded tx happened to be available
    // to piggyback on; PSTT has no such global tx, so the dummy is built
    // explicitly here instead).
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

    return complete;
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
