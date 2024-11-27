Tapyrus version 0.6.0 is now available for download at:
https://github.com/chaintope/tapyrus-core/releases/tag/v0.6.0

Please report bugs using the issue tracker at github:
https://github.com/chaintope/tapyrus-core/issues

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
https://github.com/chaintope/tapyrus-core/tarball/v0.6.0


How to Upgrade
==============

It is recommended to upgrade all older nodes to the latest release as it contains fixes to vulnerabilities. If you are running a node on tapyrus testnet follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to sart a new Tapyrus v0.6.0 node.

If you are running a private tapyrus network using versions v0.3.0, v0.4.0 or v0.4.1, v0.5.0, v0.5.1, v0.5.2 shut it down. Wait until it has completely shut down. Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to sart a new Tapyrus v0.6.0 network. 

Downgrading warning
-------------------

Tapyrus blockchain created by older versions(v0.3.0, v0.4.0 and v0.4.1) is not compatible with v0.6.0 and vice-versa.

Compatibility
==============

Tapyrus v0.6.0 is supported on three platforms, Linux, MacOS and Windows(WSL) in two CPU architectures namely x86_x64 and arm64(aarch64)

0.6.0 change log
------------------

*Dynamic Block size*

This release allows the size of each block in a Tapyrus network to be increased from the default 1MB. This is done with the help of a new xfield type. When the block size is changed using the xfield in a federation block, the new block size becomes the maximum limit for block size for every block created thereafter. Note that in order to sign blocks with the new block size, the signer network needs to be upgraded too. 

*General improvements*

- Tracing of select tracepoints/events in a live tapyrus node is possible with this release. The list of all allowed tracepoints is listed [here](doc/tracing.md).
- Improve stability of thread synchronization and mutexes used in tapyrus.
- Fix vulnerabilities inherited from bitcoin core.

*RPC*

- dumptxoutset rpc is added to create a dump of the utxo set on a tapyrus node.

*BUILD*

- use newer version of all dependencies.
- remove usage of system and thread boost libraries and use std c++17 libraries.