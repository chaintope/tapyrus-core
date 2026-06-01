Tapyrus version 0.7.2 is now available for download at:
<https://github.com/chaintope/tapyrus-core/releases/tag/v0.7.2>

Please report bugs using the issue tracker at github:
<https://github.com/chaintope/tapyrus-core/issues>

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
<https://github.com/chaintope/tapyrus-core/tarball/v0.7.2>

How to Upgrade
==============

It is recommended to upgrade all older nodes to the latest release as it contains security fixes and protocol improvements. If you are running a node on
tapyrus testnet follow the instruction in [getting_started](/doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to start a new Tapyrus v0.7.2 node.

If you are running a private tapyrus network using older versions of tapyrus-core release, shut it down. Wait until it has completely shut down.
Follow the instruction in [getting_started](/doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to start a new Tapyrus v0.7.2 network. Tapyrus blockchain created by older versions, before v0.5.0
are not compatible with v0.7.2 because of the absence of xfield in the block header. But blockchain created by v0.5.0, v0.5.1, v0.5.2, v0.6.1, v0.7.0 and v0.7.1 is compatible with v0.7.2.

Upgrade Warning — Custom OP_COLOR Scripts
------------------------------------------

Tapyrus Core versions up to 0.7.1 allowed arbitrary scripts with the `<COLOR identifier> OP_COLOR` pattern. From v0.7.2, only CP2PKH and CP2SH scripts with `OP_COLOR` are accepted. All other arbitrary scripts with `OP_COLOR` are rejected.

**Any network whose blockchain contains transactions with custom `OP_COLOR` scripts will see block rejection after upgrading to v0.7.2.** Such networks must plan their upgrade with utmost caution:

1. Identify all transactions with custom `OP_COLOR` scripts in the UTXO set and transaction history.
2. Create replacement transactions using only CP2PKH or CP2SH scripts.
3. Note that changing scripts changes transaction IDs and therefore block hashes — a complete blockchain reorg from the first rejected block onwards is required.

_**It is strongly recommended to audit and back up the UTXO set and transaction list before upgrading, in order to verify that no transaction has been lost and the UTXO set is consistent after the upgrade.**_

This warning affects only networks that have used custom `OP_COLOR` scripts. Networks that use only the standard CP2SH and CP2PKH colored coin scripts are not affected by this consensus change.

See [colored_coin.md](/doc/tapyrus/colored_coin.md#upgrade-warning) for full details.

Downgrading warning
-------------------

v0.7.2 is a release with many security fixes and stricter consensus enforcement. It is recommended not to move back to older versions after an upgrade to v0.7.2 
If you need to switch back to v0.7.1 for unavoidable reasons, there is no issue if no new CP2SH transactions were added to the node/blockchain after upgrading to v0.7.2. Otherwise these two versions are compatible. But if downgrading to versions v0.5.* or v0.6.0 the blockchain and chainstate on the node are backward compatible as long as **no xfield change** was made in the network.

Compatibility
-------------

Tapyrus v0.7.2 is supported on Linux, macOS and Windows platforms in their supported CPU architectures namely x86_64 and arm64 (aarch64).

Change Summary
==============

This release contains security fixes addressing vulnerabilities identified in a security audit of tapyrus core. There are consensus changes in relation to OP_COLOR handling: only CP2PKH and CP2SH acripts are now allowed. Also CP2SH colored coin issuing and spending has been fixed to sign and verify the inner P2SH script. All token issues now use CP2PKH by default. A new softfork mechanism that activates CP2SH script evaluationhas been deployed on the Chaintope testnet.

0.7.2 change log
================

*Security fixes:*
- [PR411](https://github.com/chaintope/tapyrus-core/pull/411) - Fix CP2SH vulnerabilities identified in security audit: prevent attacker-controlled block height from influencing aggregate key selection
- [PR412](https://github.com/chaintope/tapyrus-core/pull/412) - Fix security vulnerabilities identified in audit: introduce a global NFT and Non-reissuable token list for ensuging their uniqueness globally
- [PR413](https://github.com/chaintope/tapyrus-core/pull/413) - Fix security vulnerabilities: harden feebumper to correctly account for fees when no TPC change output is present

*Protocol improvements:*
- [PR414](https://github.com/chaintope/tapyrus-core/pull/414) - Add CP2SH colored coin softfork manager and activation gating. The Chaintope testnet (networkId 1939510133) activates at block 693367.

*Bug fixes:*
- Fix CVE-2024-52911: use-after-free crash in `ConnectBlock`
- [PR404](https://github.com/chaintope/tapyrus-core/pull/404) - Fix block file truncation during reindex when the block file exceeds 16 MB (observed on macOS and Windows). 
- Fix a crash in the genesis block disk-write path during reindex when the block index update was skipped due to a null pointer.
- Fix `ReadBlockFromDisk` using the wrong aggregate public key after an xfield rotation by threading the trusted `pindex->nHeight` instead of a `-1` sentinel.
- Fix `VerifyDB` level-3 sandbox: disconnect-tip was mutating global state. Add a `dryrun` flag to guard the erase operation.
- Fix listunspent crash on CP2SH UTXOs.
- Fix bumpfee RPC with colored coins accidentally burning the colored coins.