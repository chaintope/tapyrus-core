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

Downgrading warning
-------------------

v0.7.2 is a release with many security fixes and stricter consensus enforcement. It is recommended not to move back to older versions after an upgrade to v0.7.2 
If you need to switch back to v0.7.1 for unavoidable reasons, there is no issue if no new CP2SH transactions were added to the node/blockchain after upgrading to v0.7.2. Otherwise these two versions are compatible. But if downgrading to versions v0.5.* or v0.6.0 the blockchain and chainstate on the node are backward compatible as long as **no xfield change** was made in the network.

Compatibility
-------------

Tapyrus v0.7.2 is supported on Linux, macOS and Windows platforms in their supported CPU architectures namely x86_64 and arm64 (aarch64).

Notable changes
===============

Updated RPCs
------------

- `gettxoutsetinfo` returns a new field, `issued_colorids_hash`: a hash of the
  issued NON_REISSUABLE and NFT colorIds, which are kept in the chainstate
  database alongside the UTXO set. `hash_serialized_3` is unchanged and still
  covers only the UTXO set, so it stays comparable with older versions. To
  check that two nodes have the same chainstate, compare both fields.

P2P
---

- When a block arrives, the node now clears outstanding requests for that
  block to every peer, not only the one that sent it. A block completed from
  one peer's compact block used to leave a request to another peer pending,
  and that peer was disconnected for a block download timeout 10 minutes or
  more later.
