# Fuzz4All for Tapyrus Script (draft)

Generation-based fuzzing of Tapyrus's Script interpreter: instead of
mutating raw bytes fed to one function (what `../oss-fuzz/project/` and
`src/test/fuzz/pstt_parse_fuzz.cpp` do), Claude generates whole
syntactically-plausible Script *programs* (opcode sequences), which get
assembled and run through the real interpreter looking for crashes.

Files in this directory are **patches/additions to a real Fuzz4All
checkout** (`github.com/fuzz4all/fuzz4all`), not a standalone package --
Fuzz4All isn't vendored into this repo.

## Generation is local-only, not a CI job

`fuzz_script_generate_pool.py` is a human-run script: clone Fuzz4All,
apply the patches below (automatic, see `fuzz_script_apply_patches.py`), generate a
batch of candidates, review, commit. Generation is a judgment call
about spend and timing, made by whoever runs this, not a recurring
pipeline concern. `daily-test.yml`'s `fuzz-script-sweep` job replays the
already-committed `generated_pool/tapyrus_script/` against
`tapyrus-verify --fuzz` on its own daily schedule -- Claude and Fuzz4All
are only ever involved when a human runs this script.

## Human review, not auto-commit

Every candidate this script generates lands straight in its final home,
`generated_pool/tapyrus_script/` -- there's no separate drafts location
to wire up the way `../oss-fuzz/drafting/` has. So the review here is a
*prune*, not a landing step: the script's last action is building
`review_scripts_<run-prefix>.html` (via `fuzz_script_build_review_page.py`)
listing just this run's new candidates with a "keep" checkbox, defaulted
unchecked. Open it, check the ones worth keeping, save it back (File >
Save Page As, Webpage HTML Only -- the page's own JS keeps the checkbox
state in the saved markup), then run

```
./fuzz_script_land_approved.py <path-to-the-saved-review_scripts.html>
```

which deletes every unchecked candidate from `generated_pool/tapyrus_script/`
and leaves the checked ones in place. No git commands -- `git add` and
commit what survives yourself, same working agreement as everything else
generated in this repo.

## What's real vs. stubbed in this pass

- **`claude_model.py`** -- complete. Fuzz4All's actual `Fuzz4All/model.py`
  has no OpenAI or Anthropic backend at all (confirmed by reading the real
  source, not the README) -- only local HuggingFace StarCoder or Ollama.
  This file is a real `ClaudeModel` adapter matching `StarCoder`'s
  `.generate()` interface, following the same `claude/<model-id>` prefix
  convention already used for `ollama/<model>`, plus real per-call spend
  tracking from actual API usage (`BudgetExceededError`).
- **`fuzz_script_apply_patches.py`** -- complete. Wires `claude_model.py`'s
  `ClaudeModel` into `make_model()` and the `TAPYRUSSCRIPT` target into
  both of `make_target.py`'s dispatch functions, in a fresh Fuzz4All
  clone. Every patch is an exact-string match against Fuzz4All's real
  current source (confirmed by cloning and reading it directly, not
  guessed) and fails loudly if the match doesn't hold, rather than
  silently skipping or guessing -- that signals upstream has changed and
  this needs a matching update. `fuzz_script_generate_pool.py` runs it
  automatically; there's no hand-editing step anymore.
- **`TAPYRUSSCRIPT.py`** -- complete (correctly subclasses the real
  `Target` base class, read directly from Fuzz4All's source --
  `write_back_file`/`wrap_prompt`/`filter`/`clean`/`clean_code`/
  `validate_individual` all implemented per that base class's actual
  contract). `validate_individual` shells out to `VERIFY_BINARY_PATH` --
  `tapyrus-verify --fuzz <file>` (`src/tapyrus-verify.cpp`), built by
  `fuzz_script_generate_pool.py` before Fuzz4All runs.
- **`tapyrus-verify`** (`src/tapyrus-verify.cpp`) -- the oracle. Given one
  generated script, its `--fuzz` mode (1) assembles it into a raw
  `CScript` via `ParseScript` (`src/core_io.h`, a real library function,
  not test-only), (2) builds several (to_spend, spending) transaction
  pairs sweeping a handful of nLockTime/nSequence combinations -- not
  just one -- reusing the same shape Bitcoin's own BIP-341/342 test
  vectors use, (3) runs `VerifyScript` on each pair, printing the pair's
  hex alongside PASS/FAIL so any interesting result is reproducible via
  the same binary's plain `<to_spend_hex> <spending_hex>` mode, (4) exits
  per the convention documented in its own header comment (0 = safe,
  exit code 2 = the assembler rejected the program as not a valid Script
  -- an `LLM_WEAKNESS` result, not a bug -- a signal = a real crash).
  Deliberately lean: links `tapyrus_consensus` (script/interpreter.cpp,
  primitives/transaction.cpp, etc) plus `tapyrus_common` only for chain
  parameter selection -- no storage, no networking, no block-level
  validation. Built via `-DBUILD_SCRIPT_VERIFY=ON -DBUILD_FUZZ_TEST=ON`.
- **`tapyrus_script.yaml`** -- complete as a config, pointing at the real
  `doc/tapyrus/script.md` for prompt documentation, `no_input_prompt: true`
  to route around `auto_prompt()`'s hardcoded `NotImplementedError` for
  API-backed autoprompting (see `claude_model.py`'s docstring) -- Claude
  only drives the per-iteration generation loop, not a documentation-
  distillation step.

## Why `no_input_prompt: true`, not a documentation-distilled prompt

Fuzz4All's actual "autoprompting" feature (turning target documentation
into a good generation prompt via a chat LLM) is disabled in the real
source for anything other than a local/Ollama model -- an explicit
`raise NotImplementedError` in `target.py`'s `auto_prompt()`, not a missing
feature this integration forgot to wire up. Patching that too is possible
(it's a single method override) but out of scope for this pass; the
hand-written `trigger_to_generate_input`/`input_hint` in
`tapyrus_script.yaml` gets equivalent results for a Script-sized language
without needing that patch.
