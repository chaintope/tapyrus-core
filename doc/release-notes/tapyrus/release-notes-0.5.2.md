Tapyrus version 0.5.2 is now available for download at:
  https://github.com/chaintope/tapyrus-core/releases/tag/v0.5.2

Please report bugs using the issue tracker at github:
  https://github.com/chaintope/tapyrus-core/issues

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
  https://github.com/chaintope/tapyrus-core/tarball/v0.5.2


How to Upgrade
==============

It is recommended to upgrade all older nodes, v0.5.0 and 0.5.1 to the latest release as it contains critical bug fixes. If you are running a node on tapyrus testnet follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to sart a new Tapyrus v0.5.2 node.

If you are running a private tapyrus network using versions v0.3.0, v0.4.0 or v0.4.1, shut it down. Wait until it has completely shut down. Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to sart a new Tapyrus v0.5.2 network. Please note that tapyrus-signer network should also be upgraded following [tapyrus signer network setup](https://github.com/chaintope/tapyrus-signer/blob/master/doc/setup.md#how-to-set-up-new-tapyrus-signer-network)


Downgrading warning
-------------------

Tapyrus blockchain created by older versions(v0.3.0, v0.4.0 and v0.4.1) is not compatible with v0.5.2 and vice-versa.

Compatibility
==============

Tapyrus v0.5.2 is supported on three platforms, Linux, MacOS and Windows(WSL) in two CPU architectures namely x86_x64 and arm64(aarch64)

0.5.2 change log
------------------

*Federation Management*

Tapyrus-core version v0.5.2 contains critical bug fixes to make Federation Management reliable:
 1. Adding a new node to the tapyrus-core network after an aggregate public key update(using xField in the block header) is now supported.
 2. A node restarted after aggregate pubkey update can now use the block tree db key to persist and retrieve the list of aggpubkeys instead of using `-reindex`.
 3. A new command line argument `-reloadxfield` is added to create the block tree db entry in older nodes while upgrading.

*Token issue fee*

Fee required to issue new tokens/colored coins is made to match the network fee rate. It was _one Tapyrus_ more than the network fee rate until now.
