Tapyrus version 0.7.0 is now available for download at:
<https://github.com/chaintope/tapyrus-core/releases/tag/v0.7.1>

Please report bugs using the issue tracker at github:
<https://github.com/chaintope/tapyrus-core/issues>

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
<https://github.com/chaintope/tapyrus-core/tarball/v0.7.1>

How to Upgrade
==============

It is recommended to upgrade all older nodes to the latest release as it contains fixes to vulnerabilities. If you are running a node on
tapyrus testnet follow the instruction in [getting_started](/doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to start a new Tapyrus v0.7.1 node.

If you are running a private tapyrus network using older versions of tapyrus-core release, shut it down. Wait until it has completely shut down.
Follow the instruction in [getting_started](/doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to start a new Tapyrus v0.7.1 network. Tapyrus blockchain created by older versions, before v0.5.0
are not compatible with v0.7.1 because of the absence of xfield in the block header. But blockchain created by v0.5.0, v0.5.1, v0.5.2, v0.6.1 and v0.7.0 is compatible with v0.7.1.

Downgrading warning
-------------------

If you upgrade to a node running v0.7.1 and need to switch back to v0.6.1, there is no issue. These two versions are compatible. But if downgrading to versions v0.5.*, the blockchain and chainstate on the node are backward compatible as long as **no xfield change** was mode in the network.

Compatibility
-------------

Tapyrus v0.7.1 is supported on Linux, Macos and Windows platform in their supported CPU architectures namely x86_x64 and arm64(aarch64)

Change Summary
==============

This release is has some bug fixes that were not part of the previous release.

0.7.1 change log
================

*Bug fixes:*
- [PR391](https://github.com/chaintope/tapyrus-core/pull/391) - Fix progress bar to show IBD accurately
- [PR393](https://github.com/chaintope/tapyrus-core/pull/393) & [PR394](https://github.com/chaintope/tapyrus-core/pull/394) - Fix crash in tapyrusd during reindex when the blockfile being validated has out of order blocks and xfield change.