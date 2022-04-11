Tapyrus version 0.5.1 is now available for download at:
  https://github.com/chaintope/tapyrus-core/releases/tag/v0.5.1

Please report bugs using the issue tracker at github:
  https://github.com/chaintope/tapyrus-core/issues

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
  https://github.com/chaintope/tapyrus-core/tarball/v0.5.1  # .tar.gz


How to Upgrade
==============

If you are running a node on older version(v0.3.0, v0.4.0, v0.4.1) testnet, shut it down. Wait until it has completelyshut down. Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to sart a new Tapyrus v0.5.0 node.

If you want to running a private tapyrus network, shut down all nodes. Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to sart a new Tapyrus v0.5.1 network. As v0.5.1 makes significant changes to consensus rules, when a running network is stopped all nodes should be upgraded before restarting the network again. Please note that tapyrus-signer network should also be upgraded to v0.5.1 following [tapyrus signer network setup](https://github.com/chaintope/tapyrus-signer/blob/master/doc/setup.md#how-to-set-up-new-tapyrus-signer-network)

Downgrading warning
-------------------

Tapyrus blockchain created by older versions(v0.3.0, v0.4.0 and v0.4.1) is not compatible with v0.5.1 and vice-versa. The testnet was reset with the release of v0.5.0 wqhich is compatile with v0.5.1

Compatibility
==============

Tapyrus v0.5.1 is supported on three platforms, Linux, MacOS and Windows(WSL) in two CPU architectures namely x86_x64 and arm64(aarch64)

0.5.1 change log
------------------

*ARM64 container*

With version v0.5.1 Tapyrus is supported on ARM64 hosts. Tapyrus containers are also a vailable for ARM64 architecture

*RPC*

* getrawtransaction - version is renamed to features.
* decoderawtransaction - version is renamed to features..
