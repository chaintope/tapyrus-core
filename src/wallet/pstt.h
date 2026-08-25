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

/** Post-sign PSTT_GLOBAL_TX_MODIFIABLE mutation (see doc/tapyrus/pstt.md's
 *  Signer role).
 *  Every input signed in a single Signer call shares one sighash_type, so the
 *  accumulated constraint is computed once, here, rather than per input. A
 *  signature that doesn't allow further inputs (no ANYONECANPAY) closes
 *  Inputs-Modifiable; one that commits to every output (not SIGHASH_NONE/
 *  SIGHASH_SINGLE) closes Outputs-Modifiable; SIGHASH_SINGLE itself sets
 *  Has-SIGHASH_SINGLE so future Constructor calls must pair input/output
 *  additions. Only ever clears/sets bits -- never reopens one an
 *  earlier signing round already closed. Shared by FillPSTT (this file) and
 *  walletsignpstt (wallet/rpcwallet.cpp), which signs via its own loop rather
 *  than calling FillPSTT. Call only after at least one input was actually
 *  signed in this round. */
void ApplyPsttPostSignModifiableRules(PartiallySignedTapyrusTransaction& pstt, int sighash_type);

/** Sums per-color balances across a PSTT's inputs (via each input's attached
 *  UTXO) and outputs, keyed by ColorIdentifier -- TPC is simply the entry
 *  keyed by the default/NONE-type identifier, never a structurally separate
 *  field. Color is always derived from the relevant scriptPubKey via
 *  GetColorIdFromScript(), never assumed. Inputs with no UTXO attached yet
 *  are skipped (their color/amount isn't knowable). */
void ComputePsttColorBalances(const PartiallySignedTapyrusTransaction& pstt,
                               TxColoredCoinBalancesMap& in, TxColoredCoinBalancesMap& out);

// Looks up a key's HD derivation metadata in the wallet and, if found,
// records it. Declared here rather than wallet/rpcwallet.h so wallet/pstt.cpp
// doesn't need to include that header at all -- avoids a wallet/pstt <->
// wallet/rpcwallet circular include (rpcwallet.cpp already includes this
// header for FillPSTT, so the declaration stays visible where the function
// is defined).
void AddKeypathToMap(const CWallet* pwallet, const CKeyID& keyID, std::map<CPubKey, std::vector<uint32_t>>& hd_keypaths);

#endif // TAPYRUS_WALLET_PSTT_H
