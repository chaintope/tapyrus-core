Partially Signed Tapyrus Transaction (PSTT)
============================================

PSTT is Tapyrus's own partially-signed-transaction format, defined by
[TIP-174](https://github.com/chaintope/tips/blob/master/tip-0174.md) and
implemented in `src/pstt.h`/`src/pstt.cpp`. It replaces the Bitcoin-shaped
PSBT (BIP-174 "v0") that tapyrus-core previously inherited unmodified: PSBT
has no counterpart for Tapyrus's malleability-fixed txid, no notion of
Colored Coins, and hardcodes ECDSA-only signing.

PSTT is built on BIP-370's constructable (per-input/output) data model
instead of BIP-174's single embedded unsigned transaction: there is no
global unsigned tx record. Each input carries its own previous-txid/output-index
pair directly, which is what lets a PSTT be built up incrementally across
multiple round trips between parties (see [Fee Provider workflow](#fee-provider-workflow)
below) rather than requiring one party to fix the whole set of inputs and
outputs up front.

Key differences from BIP-174 PSBT:
* No global unsigned transaction; per-input outpoint fields instead.
* No witness fields (Tapyrus has no segwit) and no Taproot fields.
* Colored Coin aware: `PSTT_OUT_SCRIPT` may carry the `<color id> OP_COLOR`
  prefix, and CP2SH redeem-script verification accounts for it.
* Signer supports both ECDSA and Schnorr, and rejects mixing the two
  schemes within one input's signature set.

---

Wire format
-----------

A PSTT is `PSTT_MAGIC_BYTES` followed by a global key-value map, then one
key-value map per input, then one per output. Every map is terminated by an
empty key (a single `0x00` length byte) — a map with no terminator is a
parse error, not an implicit end-of-data.

```
magic bytes: 0x70 0x73 0x74 0x74 0xFF  ("pstt" + 0xFF)
```

Each record in a map is `<key><value>`, where both `<key>` and `<value>`
are themselves length-prefixed byte strings (a CompactSize byte count
followed by that many bytes) — this holds even for fixed-width values like
a `uint32_t` or a `uint256`; those still get wrapped in the same
length-prefix envelope as every other value. `<key>`'s first byte is the
record's type; any remaining key bytes are that type's keydata (e.g. a
pubkey for `PSTT_IN_PARTIAL_SIG`).

### Global types

| Name | Value | Keydata | Value | Notes |
|---|---|---|---|---|
| `PSTT_GLOBAL_XPUB` | `0x01` | `<xpub>` — the 78-byte BIP32 extended public key (4-byte version prefix + `CExtPubKey`'s 74-byte encoding) | `<fingerprint><32-bit uint>*` — master fingerprint + derivation path | Prefix must equal exactly `Params().Base58Prefix(EXT_PUBLIC_KEY)` for the running node's own network; any other prefix, including the other Tapyrus network's, is rejected at parse time |
| `PSTT_GLOBAL_TX_FEATURES` | `0x02` | none | `int32_t` | **Required.** Any value is accepted and round-tripped verbatim into `CMutableTransaction::nFeatures` on extraction — no validation is imposed here beyond what the transaction layer itself imposes |
| `PSTT_GLOBAL_FALLBACK_LOCKTIME` | `0x03` | none | `uint32_t` | Default 0 if absent |
| `PSTT_GLOBAL_INPUT_COUNT` | `0x04` | none | `<compact size uint>` | **Required.** Not stored separately from `inputs.size()`; only meaningful on the wire, to tell the parser how many input maps follow |
| `PSTT_GLOBAL_OUTPUT_COUNT` | `0x05` | none | `<compact size uint>` | **Required.** Same as above, for output maps |
| `PSTT_GLOBAL_TX_MODIFIABLE` | `0x06` | none | `uint8_t` bitfield | Absent means not modifiable. Bit 0: inputs modifiable. Bit 1: outputs modifiable. Bit 2: has a `SIGHASH_SINGLE` signature (input/output correspondence must be preserved). Bits 3-7 reserved, must be 0 |
| `PSTT_GLOBAL_VERSION` | `0xFB` | none | `uint32_t` | Default 0; a value greater than 0 is rejected at parse time |
| `PSTT_GLOBAL_PROPRIETARY` | `0xFC` | proprietary identifier | proprietary | Passed through unchanged |

Type value `0x00` is reserved (it was BIP-174's global unsigned transaction,
which has no counterpart here) and must be actively rejected, not folded
into the unknown-field bucket.

### Input types

| Name | Value | Keydata | Value | Notes |
|---|---|---|---|---|
| `PSTT_IN_UTXO` | `0x00` | none | the full previous transaction | Required before signing. No witness-UTXO shortcut exists in Tapyrus |
| `PSTT_IN_PARTIAL_SIG` | `0x02` | 33- or 65-byte public key | signature + 1-byte sighash type | Valuedata is either a DER-encoded ECDSA signature (~71-73 bytes total including the trailing sighash byte) or exactly 65 bytes (64-byte Schnorr signature + sighash byte) — no other length is legal |
| `PSTT_IN_SIGHASH_TYPE` | `0x03` | none | `int32_t` | |
| `PSTT_IN_REDEEM_SCRIPT` | `0x04` | none | script | For CP2SH, this is the redeem script alone — it excludes the `<color id> OP_COLOR` prefix that lives in the outer scriptPubKey |
| `PSTT_IN_BIP32_DERIVATION` | `0x06` | public key | `<fingerprint><32-bit uint>*` | |
| `PSTT_IN_FINAL_SCRIPTSIG` | `0x07` | none | script | Once present, an input is finalized; its other signing-only fields are stripped |
| `PSTT_IN_RIPEMD160` | `0x0a` | 20-byte hash | preimage | |
| `PSTT_IN_SHA256` | `0x0b` | 32-byte hash | preimage | |
| `PSTT_IN_HASH160` | `0x0c` | 20-byte hash | preimage | |
| `PSTT_IN_HASH256` | `0x0d` | 32-byte hash | preimage | |
| `PSTT_IN_PREVIOUS_TXID` | `0x0e` | none | `uint256` (`hashMalFix` of the previous tx) | **Required.** There is no global unsigned tx, so this and the next field are the only source of truth for what this input spends |
| `PSTT_IN_OUTPUT_INDEX` | `0x0f` | none | `uint32_t` | **Required** |
| `PSTT_IN_SEQUENCE` | `0x10` | none | `uint32_t` | Default `0xFFFFFFFF` if absent |
| `PSTT_IN_REQUIRED_TIME_LOCKTIME` | `0x11` | none | `uint32_t` | Must be `>= 500000000`; out-of-range values are rejected at parse time, not deferred |
| `PSTT_IN_REQUIRED_HEIGHT_LOCKTIME` | `0x12` | none | `uint32_t` | Must be `> 0` and `< 500000000`; same parse-time bounds check |
| `PSTT_IN_PROPRIETARY` | `0xFC` | proprietary identifier | proprietary | |

Reserved and must be rejected, not silently treated as unknown: `0x01`
(witness UTXO), `0x05` (witness script), `0x08` (finalized scriptWitness),
`0x09` (proof-of-reserves commitment), and the Taproot-related type values
from BIP-371 (`0x13`-`0x18`) — none have a Tapyrus counterpart.

`PSTT_IN_PREVIOUS_TXID`/`PSTT_IN_OUTPUT_INDEX` are this wire format's own
field names (mirrored by `PSTTInput`'s `previous_txid`/`prev_out_index`
members in C++), distinct from the RPC surface: every PSTT RPC that takes
this pair as a user-facing argument (`createpstt`, `walletcreatefundedpstt`,
`addinputtopstt`, `addinputoutputpairtopstt`) uses `txid`/`vout` uniformly,
matching `createrawtransaction`'s existing convention. There is no RPC that
exposes `previous_txid`/`output_index` as an argument or help-text name.

### Output types

| Name | Value | Keydata | Value | Notes |
|---|---|---|---|---|
| `PSTT_OUT_REDEEM_SCRIPT` | `0x00` | none | script | |
| `PSTT_OUT_BIP32_DERIVATION` | `0x02` | public key | `<fingerprint><32-bit uint>*` | |
| `PSTT_OUT_AMOUNT` | `0x03` | none | `int64_t` | **Required** |
| `PSTT_OUT_SCRIPT` | `0x04` | none | script | **Required.** Includes the `<color id> OP_COLOR` prefix when the output is colored |
| `PSTT_OUT_PROPRIETARY` | `0xFC` | proprietary identifier | proprietary | |

Reserved and must be rejected: `0x01` (witness script) and the
Taproot-related type values from BIP-371 (`0x05`-`0x07`).

---

Determining the locktime
-------------------------

A PSTT has no single stored locktime field; it is computed from the
constituent inputs whenever one is needed (identification, signing,
extraction):

1. If no input sets `PSTT_IN_REQUIRED_TIME_LOCKTIME` or
   `PSTT_IN_REQUIRED_HEIGHT_LOCKTIME`, the locktime is
   `PSTT_GLOBAL_FALLBACK_LOCKTIME` (default 0).
2. Otherwise, each input that sets at least one of the two fields
   constrains the locktime to a kind: `{height}` if only height is set,
   `{time}` if only time is set, `{height, time}` if both are set (an
   input setting neither imposes no constraint). Intersect the constraint
   sets across every input that specifies at least one kind. An empty
   intersection means no locktime works for every input — the PSTT is
   locktime-invalid, and no role primitive should sign or extract it.
   When both kinds remain acceptable, height is preferred.
3. The locktime is the maximum of the chosen kind's field across the
   inputs that specify it.

---

Unique identification
----------------------

A PSTT's identifier is the `hashMalFix` of the transaction it would
materialize with every input's sequence number forced to zero (this,
rather than the actual sequence numbers, is what keeps the identifier
stable as inputs get filled in with real sequence numbers over the course
of construction and signing). Two PSTTs are combinable only if they share
an identifier.

---

Roles
-----

Every role below is an independently invokable operation — nothing in
tapyrus-core hardcodes a fixed pipeline ordering them, since the same
transaction can flow through the same role more than once (the Constructor
role in particular is designed for multi-round-trip incremental
construction across separate parties, not a single call).

* **Creator** — builds a bare PSTT from a caller-supplied set of inputs and
  outputs (either of which may be empty).
* **Constructor** — appends inputs/outputs to an existing PSTT. Only ever
  appends; never removes or reorders what is already present. Refuses to
  add an input whose required locktime would make the PSTT's already-signed
  content locktime-invalid.
* **Updater** — attaches externally known data (UTXOs, redeem scripts,
  BIP32 derivation paths) without altering which inputs/outputs exist. Must
  not change an input's sequence number once that input carries a
  signature, or once any other input carries a `SIGHASH_ALL`-without-
  `SIGHASH_ANYONECANPAY` signature (either case would silently invalidate
  a commitment already made).
* **Signer** — produces a partial signature for one input. Refuses to sign
  an input with no `PSTT_IN_UTXO`, or whose UTXO's txid does not match
  `PSTT_IN_PREVIOUS_TXID`, or whose redeem script does not hash to the
  value committed in the scriptPubKey (accounting for the CP2SH color-id
  prefix), or that would use `SIGHASH_SINGLE` on an input whose index has
  no corresponding output, or that would add a signature whose scheme
  (ECDSA/Schnorr) conflicts with a signature already present on that
  input. After signing, updates `PSTT_GLOBAL_TX_MODIFIABLE` based on the
  sighash type(s) just used: a signature without `SIGHASH_ANYONECANPAY`
  closes Inputs Modifiable (no further inputs can be added without
  invalidating it); one that commits to every output (not `SIGHASH_NONE`/
  `SIGHASH_SINGLE`) closes Outputs Modifiable; `SIGHASH_SINGLE` itself sets
  the Has-`SIGHASH_SINGLE` bit, after which the Constructor may only add
  inputs and outputs together, never one without the other. This only ever
  clears Inputs/Outputs Modifiable or sets Has-`SIGHASH_SINGLE` — it never
  reopens a bit an earlier signing round already closed.
* **Combiner** — merges two or more PSTTs that share an identifier.
  Per-field conflict policy on a mismatch: `PSTT_IN_PARTIAL_SIG`,
  `PSTT_IN_FINAL_SCRIPTSIG`, and `PSTT_IN_UTXO` refuse (signature/UTXO
  disagreements are exactly the disagreements that must not be silently
  dropped); `PSTT_IN_REDEEM_SCRIPT` and any `*_BIP32_DERIVATION` record
  must match exactly (both parties should have derived the identical
  value, so a mismatch signals a real problem); every other field,
  including unrecognized/proprietary records, is harmless to duplicate and
  the first-seen value wins.
* **Input Finalizer** — for each input whose signature requirements are
  satisfied, synthesizes `PSTT_IN_FINAL_SCRIPTSIG` and strips the
  now-redundant signing-only fields (partial sigs, sighash type, redeem
  script, BIP32 derivation, preimages), keeping the outpoint fields, UTXO,
  sequence, required locktime, and any unrecognized records. Refuses while
  either modifiable flag is still set.
* **Transaction Extractor** — once every input is finalized, assembles and
  returns the network-serialized transaction.

---

Fee Provider workflow
----------------------

TIP-174 exists in part to support a wallet that holds only Colored Coins:
since fees are always paid in TPC, such a wallet cannot complete a
transaction alone. The Fee Provider pattern lets a second party — one that
does hold TPC — complete the transaction, using only the Constructor,
Updater, Signer, and Combiner primitives above; tapyrus-core does not
hardcode the two-party protocol itself, only the primitives that make it
possible. The fee provider's side of both variants below is exposed as a
single wallet RPC, `walletfundpsttfee`, taking a `mode` argument of
`"noninteractive"` or `"interactive"`.

Both variants share the same starting point: the token-only party
constructs a PSTT covering their own inputs/outputs with the Inputs
Modifiable flag left set (so a TPC input can be added later) and signs
with `SIGHASH_ALL | SIGHASH_ANYONECANPAY` (so more inputs can be added
without invalidating their signature). The two variants differ in how the
fee provider then supplies its input:

* **Non-interactive** — the fee provider supplies one already-known TPC
  outpoint directly (`walletfundpsttfee`'s `fee_input` argument). Its
  color must be verified (derived from the referenced output's
  scriptPubKey, never assumed from the caller's intent) before it is
  added — a colored-coin outpoint must be rejected, not silently accepted
  as if it were TPC. Adding a fee input with no paired output is refused
  outright once the PSTT has a `SIGHASH_SINGLE` signature (see the Signer
  role above on Has-`SIGHASH_SINGLE`), since a standalone input add is
  exactly what that state forbids.
* **Interactive** — the fee provider runs its own coin selection
  (`walletfundpsttfee` with no `fee_input`), which may add more than one TPC
  input, plus at most one TPC change output — none at all if the selected
  inputs match the target exactly or if the change would be dust, in which
  case it is folded into the fee instead. The same Has-`SIGHASH_SINGLE`
  restriction applies, generalized to however many inputs/outputs were
  actually added: since `addinputoutputpairtopstt` only ever adds one input
  and one output together, any input/output count mismatch from coin
  selection (not just "some inputs, zero outputs") is refused the same way
  the non-interactive case is.

Either way, the fee provider signs its own new input with `SIGHASH_ALL`,
and either party finalizes and extracts once both signatures are present.

---

Colored Coin balance verification
-----------------------------------

Any code path that needs to reason about "how much of which color moves
through this PSTT" (a fee provider deciding whether to add its input, a
wallet double-checking a PSTT before signing) should compute one balance
map keyed by color identifier, where TPC is simply the entry keyed by the
default/`NONE`-type color identifier — never a structurally separate
field. Color is always derived from a scriptPubKey via
`GetColorIdFromScript()`, never assumed from an RPC parameter's name.
