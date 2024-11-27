Tapyrus version 0.3.0 is now available for download at:
  https://github.com/chaintope/tapyrus-core/releases/tag/v0.3.0

Please report bugs using the issue tracker at github:
  https://github.com/chaintope/tapyrus-core/issues

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
  https://github.com/chaintope/tapyrus-core/tarball/v0.3.0  # .tar.gz


How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down. Follow the instruction in [getting_started](https://github.com/chaintope/tapyrus-core/blob/v0.3.0/doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to sart a new Tapyrus node.


Downgrading warning
-------------------

Tapyrus blockchain created by older versions(before v0.3.0) is not compatible with v0.3.0 and vice-versa. The testnet is reset with the release of v0.3.0.

Compatibility
==============

Tapyrus v.0.3.0 is supported on two platforms : Linux and  MacOS 


0.3.0 change log
------------------

*Incompatible Changes*

* Remove Version bits softfork (BIP9) support from Tapyrus.
* Remove command line parameter 'signblockthreshold'. From v0.3.0 it is managed on Tapyrus signer.
* Added 'aggregate public key' parameter to block header. It is no longer a command line parameter.
* Support Schnorr signature in Tapyrus libsecp256k1.
* Upgraded block proof and its verification from ECDSA multi signature to fixed size Schnorr signature based threshold signature.
* P2SH-Segwit like transactions are non-standard 

*Wallet*

* Remove P2SH-segwit and Bech32 addresses types.

*JSON-RPC API*

* Allow signing using Schnorr signature in `signtransactionwithwalle` and `signrawtransaction` RPCs

*P2P networking*

* Reset Tapyrus protocol version to 10000. This is the miminum supported peer protocol version in v0.3.0

*Internal codebase*

* Updated binary names and build scripts to 'Tapyrus'
* Additional unit tests for scripts and script flags
* Functional tests for schnorr signatures and block proof

*Packaging*

* Single packaging of tapyrus-core and tapyrus-signer binaries.
* Support docker images to run tapyrus testnet node.
* Tapyrus-genesis - new tool to create a tapyrus genesis block.
