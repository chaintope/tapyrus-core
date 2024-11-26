Tapyrus version 0.4.0 is now available for download at:
  https://github.com/chaintope/tapyrus-core/releases/tag/v0.4.0

Please report bugs using the issue tracker at github:
  https://github.com/chaintope/tapyrus-core/issues

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
  https://github.com/chaintope/tapyrus-core/tarball/v0.4.0  # .tar.gz


How to Upgrade
==============

If you are running a node on older version(v0.3.0) testnet, shut it down. Wait until it has completelyshut down. Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to sart a new Tapyrus v0.4.0 node.

If you want to running a private tapyrus network, shut down all nodes. Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to sart a new Tapyrus v0.4.0 network. Please note that tapyrus-signer network should also be upgraded to v0.4.0 following [tapyrus signer network setup](https://github.com/chaintope/tapyrus-signer/blob/master/doc/setup.md#how-to-set-up-new-tapyrus-signer-network)


Downgrading warning
-------------------

Tapyrus blockchain created by older versions(v0.3.0) is not compatible with v0.4.0 and vice-versa. The testnet is reset with the release of v0.4.0.

Compatibility
==============

Tapyrus v.0.4.0 is supported on two platforms : Linux and  MacOS 


0.4.0 change log
------------------

*Tapyrus-core*

* New 'networkid' parameter to configure multiple independent Tapyrus networks, each using a different networkid.
* New 'addseeder' parameter to configure dns seeder.
* DNS lookup using networkid and host.
* Changed fixed size 'aggregate public key' in block header into variable length multipurpose xfieldType and xfield pair.
* Support federation management using xfieldType and xfield pair.
* Reduced the number of supported network modes from three(main, testnet and regtest) to two(prod and dev).
* Relax standard transaction acceptance criteria to allow custom scripts. 

*Wallet*

* Removed coinbase maturity limitaion and immature balance.
* Remove segwit address support from release version.

*Tapyrus-QT GUI*

* New Tapyrus-QT GUI is available from v0.4.0
* Support to send and receive TPC using legacy addresses.

*JSON-RPC API*

* Report network mode, chain id and aggregate pubkey change history in getblockchaininfo RPC.
* Fixed bug preventing usage of testproposedblock RPC from tapytus-cli.
* Restrict invalidateblock RPC usage on blocks older than the last confirmed federation block.

*Internal codebase*

* Removed protobuf and openssl dependencies.
* BIP70 Payment protocol is not supported on Tapyrus.
* Additional unit tests for standsrd scripts and block header enhancements.
* Functional test framework improvements to support networkid and federation management.

*Packaging*

* Single packaging of tapyrus binaries with Tapyrus-QT gui and tapyrus-seeder.
* Support Snappy software deployment and package management system.
