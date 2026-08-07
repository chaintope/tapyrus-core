# Source of `tip174_valid.json` / `tip174_invalid.json`

Fetched from:

* Repository: `chaintope/tips`
* Branch: `docs/174_add_pstt_tip`
* Commit: `38fb1a4440f2f077c8c1a18076c8cdc3b53ce4da`
* Path: `tip-0174/valid.json`, `tip-0174/invalid.json`

Pinned deliberately (per the PSTT implementation plan, §9a/Phase 0): a bare branch name
would let a later fixture regeneration silently drift the test vectors this suite asserts
against. Bumping to a newer commit is a deliberate, reviewable action — re-fetch the two
files from the new commit, update the commit hash above, and re-run the full `rpc_pstt.py`
suite (including the `--scheme SCHNORR` variant) to confirm nothing regressed.

See `tip-0174/README.md` in that repository for the fixture schema and generation
conventions (dev-network parameters, deterministic key derivation, RFC 6979 nonces).
