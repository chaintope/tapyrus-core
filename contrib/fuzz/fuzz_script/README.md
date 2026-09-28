# fuzz_script: Claude Code-generated Tapyrus Script programs

Generation-based fuzzing of Tapyrus's Script interpreter: instead of
mutating raw bytes fed to one function (what `../oss-fuzz/project/` and
`src/test/fuzz/fuzz_code/pstt_parse_fuzz.cpp` do), Claude Code writes
whole syntactically-plausible Script *programs* (opcode sequences),
which get assembled and run through the real interpreter looking for
crashes.

No paid API call anywhere: generation happens in an interactive Claude
Code session in this checkout, the same way fuzz_code's harnesses are
drafted (see `../oss-fuzz/drafting/README.md`). Nothing here needs an
`ANTHROPIC_API_KEY`.

## Steps

All local, human-run -- never a CI job. `daily-test.yml`'s
`fuzz-script-sweep` job replays the already-committed
`src/test/fuzz/fuzz_scripts/` pool against `tapyrus-verify --fuzz` on its
own daily schedule, with no AI involved.

1. **Setup** -- build the oracle:

   ```sh
   ./fuzz_script_step1_build_verify.py
   ```

   Builds `tapyrus-verify` (`src/tapyrus-verify.cpp`) with
   `-DBUILD_SCRIPT_VERIFY=ON -DBUILD_FUZZ_TEST=ON` and ASan/UBSan into
   `build_fuzz_verify/` -- the same configuration and directory name the
   nightly sweep uses. Incremental on re-runs.

2. **Generate** -- ask Claude Code, in this checkout, to write a batch of
   candidates. What to ask for:

   - One program per file, written to
     `src/test/fuzz/fuzz_scripts/<YYYYMMDD>_<short_slug>.txt`, where
     `<YYYYMMDD>` is today's UTC date (the default prefix step 2
     validates) and the slug says what the program exercises.
   - Short programs in `ParseScript` mnemonic form, as accepted by
     `src/core_read.cpp`: opcode names with or without the `OP_` prefix,
     small integers as bare numbers, and raw pushes as `0x`-prefixed
     hex (e.g. `OP_DUP OP_HASH160 0x14 0x<40 hex chars> OP_EQUALVERIFY
     OP_CHECKSIG`).
   - Mix arithmetic, stack, flow-control and signature-check opcodes in
     unusual combinations, using `doc/tapyrus/script.md` as the
     reference -- including Tapyrus-specific opcodes (`OP_COLOR`,
     `OP_CHECKDATASIG`, ...) and edge cases such as boundary-sized
     numbers, unbalanced `IF`/`ELSE`/`ENDIF`, and deep stacks.
   - Read the existing pool first and avoid near-duplicates of what's
     already there -- diversity across the pool is the point.

3. **Validate**:

   ```sh
   ./fuzz_script_step2_validate.py
   ```

   Runs `tapyrus-verify --fuzz` on every candidate with today's prefix
   (`--run-prefix` to pick another batch) and prints one status per
   candidate: `safe`, `rejected`, `crash`, `failure` or `timeout`.
   `rejected` candidates (exit 2, not valid Script -- they can never
   reach the interpreter) are deleted automatically, with the
   assembler's reason printed so the next batch can avoid the same
   mistake. A `crash` is a real Tapyrus bug: keep the candidate so the
   committed pool reproduces it, and report it.

4. **Review and commit** -- the new candidates are ordinary uncommitted
   files in `src/test/fuzz/fuzz_scripts/`: look over `git status`/`git
   diff`, delete (or ask Claude Code to delete) any not worth keeping,
   then `git add` and commit what's left yourself. None of the scripts
   here run git commands.

## Why Fuzz4All was removed

This directory used to patch a [Fuzz4All](https://github.com/fuzz4all/fuzz4all)
checkout to generate candidates with Claude over the Anthropic API,
billed per call to whoever ran it. Fuzz4All can't be pointed at an
interactive Claude Code session instead: its generation loop needs a
model it can call programmatically once per candidate, and upstream
only supports local StarCoder or Ollama models for that. Driving it
through Claude Code would mean a non-interactive `claude -p` call per
candidate -- metered usage again, not an interactive session. So the
whole Fuzz4All integration went, not just the API key.

What that gives up is Fuzz4All's prompt evolution (each iteration's
prompt built from earlier examples). Step 2's "read the existing pool
first, avoid near-duplicates" instruction is the replacement: Claude
Code sees the whole pool directly rather than one prior example at a
time.

## The oracle

`tapyrus-verify --fuzz <file>` (1) assembles the file into a raw
`CScript` via `ParseScript` (`src/core_io.h`), (2) builds several
(to_spend, spending) transaction pairs sweeping a handful of
nLockTime/nSequence combinations, reusing the shape Bitcoin's own
BIP-341/342 test vectors use, (3) runs `VerifyScript` on each pair,
printing the pair's hex alongside PASS/FAIL so any interesting result is
reproducible via the same binary's plain `<to_spend_hex> <spending_hex>`
mode, and (4) exits per the contract in its own header comment (0 =
ran cleanly, 2 = rejected by the assembler, killed by a signal = a real
crash). It links `tapyrus_consensus` plus `tapyrus_common` only for
chain parameter selection -- no storage, no networking, no block-level
validation.
