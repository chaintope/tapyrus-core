Tapyrus version 0.6.0 is now available for download at:
https://github.com/chaintope/tapyrus-core/releases/tag/v0.6.0

Please report bugs using the issue tracker at github:
https://github.com/chaintope/tapyrus-core/issues

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
https://github.com/chaintope/tapyrus-core/tarball/v0.6.0


How to Upgrade
==============

It is recommended to upgrade all older nodes to the latest release as it contains fixes to vulnerabilities. If you are running a node on 
tapyrus testnet follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to start a new Tapyrus v0.6.0 node.

If you are running a private tapyrus network using older versions of tapyrus-core release, shut it down. Wait until it has completely shut down. 
Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to sart a new Tapyrus v0.6.0 network. Tapyrus blockchain created by older versions, before v0.5.0 
are not compatible with v0.6.0 because of the absence of xfield in the block header. But blockchain created by v0.5.0, v0.5.1 and v0.5.2 is compatible 
with v0.6.0.

Downgrading warning
-------------------

If you upgrade to a node running v0.6.0 and need to switch back to an older version, the blockchain and chainstate on the node are backward compatible as 
long as **no xfield change** was mode in the network.

Dynamic Block size
-------------------

This release allows the size of each block in a Tapyrus network to be increased from the default 1MB. This is done with the help of a new xfield type.
When the block size is changed using the xfield in a federation block, the new block size becomes the maximum limit for every block created thereafter. 
A tapyrus node could create a block of upto the `MAX BLOCK SIZE` using the transactions in its mempool. Note that in order to sign blocks of the new 
block size, the signer network needs to be upgraded too. The height and xfield value, in this case, _max block size_ are configured in the signer network. 
This is essential to create the federation block. ALl xfields supported in Tapyrus are listed  [here ](doc/tapyrus/signedblocks.md).

_**Warning:**_ 
- Ensure that all nodes in an older network are upgraded before attempting to change the block size limit. All older nodes reject blocks with
are bigger than 1MB. So the network could split, older nodes could create a fork chain which would be lost
when those nodes upgrade.
- A block size change cannot be reversed in a Tapyrus network. If the old block size needs to be restored in a network for unforeseen reasons, another federation
block with the old block size needs to be created. This needs to follow the signer network configuration as well.

Compatibility
-------------

Tapyrus v0.6.0 is supported on three platforms, Linux, MacOS and Windows(WSL) in two CPU architectures namely x86_x64 and arm64(aarch64)

0.6.0 change log
================

*General improvements*

- [PR280](https://github.com/chaintope/tapyrus-core/pull/280), [PR281](https://github.com/chaintope/tapyrus-core/pull/281), Tracing of select tracepoints/events in a live tapyrus node is possible with this release. The list of all allowed tracepoints is listed [here](doc/tracing.md).
- [PR276](https://github.com/chaintope/tapyrus-core/pull/276) Improve stability of thread synchronization and mutexes used in tapyrus
- [PR262](https://github.com/chaintope/tapyrus-core/pull/262) Check that uncompressed public keys are not accepted by tapyrus-genesis
- [PR286](https://github.com/chaintope/tapyrus-core/pull/286) Block height is standardized across the code to 32 bit from 64 bit
- [PR301]((https://github.com/chaintope/tapyrus-core/pull/301)) Upgrade CI to ubuntu22 and port ripemd160 implementation from bitcoin


*Fix vulnerabilities inherited from bitcoin core:*
- [PR312](https://github.com/chaintope/tapyrus-core/pull/312) Disclosure of CPU DoS due to malicious P2P message (≤ version 0.19.2)
- [PR313](https://github.com/chaintope/tapyrus-core/pull/313) Disclosure of memory DoS due to malicious P2P message (≤ version 0.19.2)
- [PR314](https://github.com/chaintope/tapyrus-core/pull/314) Disclosure of CPU DoS / stalling due to malicious P2P message (≤ version 0.17.2) 
- [PR315](https://github.com/chaintope/tapyrus-core/pull/315) Disclosure of netsplit due to malicious P2P messages by first 200 peers (≤ version 0.20.1)
- [PR317](https://github.com/chaintope/tapyrus-core/pull/317) Disclosure of remote crash due to addr message spam 
- [PR318](https://github.com/chaintope/tapyrus-core/pull/318) Disclosure of hindered block propagation due to mutated blocks 
- [PR319](https://github.com/chaintope/tapyrus-core/pull/319) Disclosure of DoS due to inv-to-send sets growing too large 
- [PR318](https://github.com/chaintope/tapyrus-core/pull/318) Disclosure of CVE-2024-35202
- [PR318](https://github.com/chaintope/tapyrus-core/pull/318) Disclosure of hindered block propagation due to stalling peers

*Bug fixes*
- [PR275](https://github.com/chaintope/tapyrus-core/pull/275) Fix crash in reloadxfield/ reindex crash

*RPC*

- [PR308](https://github.com/chaintope/tapyrus-core/pull/308) dumptxoutset rpc is added to create a dump of the utxo set on a tapyrus node

*BUILD*

- Use newer version of all dependencies as listed [here](doc/dependencies.md)
  - [PR272](https://github.com/chaintope/tapyrus-core/pull/272) Upgrade leveldb to 1.23 release version
  - [PR249](https://github.com/chaintope/tapyrus-core/pull/249) Remove unused dependencies from Tapyrus-core
  - [PR250](https://github.com/chaintope/tapyrus-core/pull/250) upgrade boost to 1.81
- [PR270](https://github.com/chaintope/tapyrus-core/pull/270), [PR269](https://github.com/chaintope/tapyrus-core/pull/269), [PR268](https://github.com/chaintope/tapyrus-core/pull/268) Remove usage of system and thread boost libraries and use std c++17 libraries
