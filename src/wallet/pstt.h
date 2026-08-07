// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_WALLET_PSTT_H
#define TAPYRUS_WALLET_PSTT_H

#include <coloridentifier.h>
#include <pstt.h>

class CWallet;

/** The real Updater (see doc/tapyrus/pstt.md): attaches PSTT_IN_UTXO from
 *  mapWallet for any input that doesn't already have one, PSTT_IN_REDEEM_SCRIPT
 *  via the wallet's own SigningProvider::GetCScript, *_BIP32_DERIVATION records
 *  for both inputs and outputs (if bip32derivs), and never adds/removes/
 *  reorders inputs or outputs -- only ever mutates fields on what's already
 *  there.
 *
 *  If sign is true, also acts as the Signer: attempts SignPSTTInput on every
 *  input that isn't already finalized, using the wallet's real keys. If sign
 *  is false, the same signing pass still runs but through a public-keys-only
 *  view of the wallet (PublicOnlySigningProvider), so redeem scripts and
 *  BIP32 paths still get discovered without producing any signature.
 *
 *  Returns true iff every input that has a UTXO now carries a complete
 *  signature (an input with no UTXO yet is not this function's problem to
 *  solve -- see PSTTSignResult::MISSING_UTXO -- it's left for a later
 *  Updater/Signer round, exactly as an incremental multi-party PSTT expects). */
bool FillPSTT(const CWallet* pwallet, PartiallySignedTapyrusTransaction& pstt, int sighash_type, bool sign,
              bool bip32derivs, SignatureScheme sigScheme = SignatureScheme::ECDSA);

/** Sums per-color balances across a PSTT's inputs (via each input's attached
 *  UTXO) and outputs, keyed by ColorIdentifier -- TPC is simply the entry
 *  keyed by the default/NONE-type identifier, never a structurally separate
 *  field. Color is always derived from the relevant scriptPubKey via
 *  GetColorIdFromScript(), never assumed. Inputs with no UTXO attached yet
 *  are skipped (their color/amount isn't knowable). */
void ComputePsttColorBalances(const PartiallySignedTapyrusTransaction& pstt,
                               TxColoredCoinBalancesMap& in, TxColoredCoinBalancesMap& out);

#endif // TAPYRUS_WALLET_PSTT_H
