# fuzz_script candidate pool

AI-generated candidate Script programs, grouped into batch files named
`<YYYYMMDD>_<batch_slug>.txt` after their generation date. Each batch
file holds one program per line in `ParseScript` mnemonic form; the `#`
comment directly above a program names it, blank lines separate entries
and sections, and `<empty>` stands for the empty script. A candidate is
identified as `<batch file>:<line>`. The format is defined in
`contrib/fuzz/fuzz_script/fuzz_script_pool.py`, which every tool reads
the pool through.

| Batch | Contents |
| --- | --- |
| `20260908_initial_pool.txt` | The original hand-written candidates, plus branching: `IF`/`NOTIF`/`ELSE`/`ENDIF` semantics, condition encodings, nesting, malformed conditionals, conditions shaped by stack and logic opcodes, early exits, what still counts in unexecuted branches, and stack/op-count limits |
| `20260928_opcode_coverage.txt` | Every Tapyrus Script opcode used at least once (115 of 115) |
| `20260929_colorid_opcolor.txt` | colorid and `OP_COLOR`: element validity matrix, colorids delivered by other opcodes, branch/multiple/position rules, CP2PKH/CP2SH/burn templates and near-misses, CLTV/CSV, signature opcodes, and every other opcode combined with `OP_COLOR` |
| `20260929_sig_checks.txt` | Signature checks only: real ECDSA/Schnorr `CHECKDATASIG` baselines, strict-DER violations, out-of-range and negated scalars and points, Schnorr R.y residuosity, ECDSA/Schnorr length confusion, pubkey encodings, hashtypes, NULLFAIL/NULLDUMMY/multisig |

`daily-test.yml`'s `fuzz-script-sweep` job tests a rotating,
time-bounded window of this pool against `tapyrus-verify --fuzz` every
day: it exports each candidate to its own temporary file, then each run
starts at a different offset (keyed off the workflow's own run number)
and walks forward until either its time budget runs out or it laps back
to its starting point, so successive runs gradually cover the whole pool
instead of always retesting the same prefix.

Unlike `src/test/fuzz/fuzz_seed_pool/`, these are not mutation seeds --
there is no libFuzzer engine here. Each candidate is fed to
`tapyrus-verify --fuzz` directly and unchanged.

The pool grows locally, by hand: Claude Code writes a new batch file
straight into this directory in an interactive session (no paid API
call, no `ANTHROPIC_API_KEY`), and
`contrib/fuzz/fuzz_script/fuzz_script_step2_validate.py` checks it
against a local sanitizer build of `tapyrus-verify`, removing any
candidate the assembler rejects. What's left is reviewed with `git diff`
and committed by hand -- see `contrib/fuzz/fuzz_script/README.md`.

## What a passing candidate does and does not tell you

`tapyrus-verify --fuzz` is a script-level oracle: it calls `VerifyScript()`
on a synthetic to_spend/spending pair. A candidate that passes is
accepted by the script interpreter under the flags being tested, and
nothing more. "Accepted under the consensus script flags" and "accepted
by consensus" are different statements, and results from this pool only
ever support the first. Two limitations follow from how the oracle is
built:

- **The scriptSig is always empty.** Rules that only act on a non-empty
  scriptSig cannot be reached from this pool, including the
  `SCRIPT_VERIFY_SIGPUSHONLY` push-only check and CP2SH redeem-script
  evaluation (`SCRIPT_VERIFY_CP2SH_COLORED`, and `OP_COLOR` inside a
  redeem script). Not seeing a failure there is a limitation of the
  oracle, not evidence that there is none.
- **There is no real transaction, UTXO set or block height.** Every rule
  enforced at transaction level in `validation.cpp` is structurally out
  of reach. In particular, script-level acceptance says nothing about:
  - `CheckColorIdentifierValidity()`: colored scriptPubKeys that are not
    exactly CP2PKH or CP2SH, on outputs or spent coins, once
    `CP2SH_COLORED` is active (`bad-txns-nonstandard-opcolor`); token
    output values of zero or less (`bad-txns-token-value`); colored
    outputs with no matching input or valid issuance
    (`bad-txns-token-noinput`); NFT output values other than 1
    (`bad-txns-nft-amount`); more than one output per NFT colorId
    (`bad-txns-nft-output-count`); and re-issuing a non-reissuable or NFT
    colorId (`bad-txns-colorid-already-issued`).
  - `VerifyTokenBalances()`: token outputs exceeding token inputs per
    colorId (`bad-txns-token-balance`) and the TPC fee requirements for
    token transactions (`bad-txns-token-without-fee`,
    `bad-txns-token-insufficient`).
  - The rest of transaction validation: `CheckTransaction` and
    `CheckTxInputs`, sigop limits, and finality and BIP68 sequence locks
    against a real chain. CHECKLOCKTIMEVERIFY and CHECKSEQUENCEVERIFY
    here only compare against the synthetic transaction's own nLockTime
    and nSequence.

This matters most for the colorid and `OP_COLOR` batch. Many of its
candidates pass `VerifyScript()`, including ones that place `OP_COLOR`
where no CP2PKH or CP2SH template allows it, yet a transaction paying to
them would be rejected with `bad-txns-nonstandard-opcolor` once
`CP2SH_COLORED` is active. When a colored candidate looks suspiciously
permissive, `CheckColorIdentifierValidity()` and `VerifyTokenBalances()`
in `validation.cpp` are where to look next.
