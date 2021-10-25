Tapyrus version 0.5.0 is now available for download at:
  https://github.com/chaintope/tapyrus-core/releases/tag/v0.5.0

Please report bugs using the issue tracker at github:
  https://github.com/chaintope/tapyrus-core/issues

Project source code is hosted at github; you can get
source-only tarballs/zipballs directly from there:
  https://github.com/chaintope/tapyrus-core/tarball/v0.5.0  # .tar.gz


How to Upgrade
==============

If you are running a node on older version(v0.3.0, v0.4.0, v0.4.1) testnet, shut it down. Wait until it has completelyshut down. Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-node-on-tapyrus-testnet) to sart a new Tapyrus v0.5.0 node.

If you want to running a private tapyrus network, shut down all nodes. Follow the instruction in [getting_started](doc/tapyrus/getting_started.md#how-to-start-a-new-tapyrus-network) to sart a new Tapyrus v0.5.0 network. As v0.5.0 makes significant changes to consensus rules, when a running network is stopped all nodes should be upgraded before restarting the network again. Please note that tapyrus-signer network should also be upgraded to v0.5.0 following [tapyrus signer network setup](https://github.com/chaintope/tapyrus-signer/blob/master/doc/setup.md#how-to-set-up-new-tapyrus-signer-network)

Downgrading warning
-------------------

Tapyrus blockchain created by older versions(v0.3.0, v0.4.0 and v0.4.1) is not compatible with v0.5.0 and vice-versa. The testnet is reset with the release of v0.5.0.

Compatibility
==============

Tapyrus v0.5.0 is supported on three platforms : Linux, MacOS and Windows(WSL)

0.5.0 change log
------------------

*Colored coin*

With version v0.5.0 Tapyrus blockchain supports tokens or colored coins. Tapyrus consensus, script and wallet layers have been enhanced to support the same. Apart from TPC, the default tapyrus coin, other tokens like NFTs, single issue tokens and reissuable tokens are now supported. Tokens may be issued, sent or received and burnt on Tapyrus blockchain. Complete specification of colored coins in Tapyrus can be found in [[Tapyrus colored coin](../../tapyrus/colored_coin.md)]

*Script*

* OP_COLOR opcode has been added to identify and process token/colorid in a transaction script.

*Colored coin wallet*

Tapyrus core wallet now supports colored coins. Coin issue, transfer and burn can be performed using tapyrus core wallet. New RPCs have been added to support these operations.

* getcolor
* issuetoken
* transfertoken
* burntoken
* reissuetoken

*RPC overhaul*

Tapyrus RPCs has been modified to remove old deprecated features and parameters. Account API support has been removed. Colored coin support has been added to other RPCs as follows.

* getnewaddress - addresstype parameter has been removed and color parameter has been added.
* getrawchangeaddress - address type parameter has been removed and color parameter has been added.
* addmultisigaddress - address type parameter has been removed.
* getreceivedbyaddress - minconf parameter has been removed.
* getreceivedbylabel - minconf parameter has been removed.
* getbalance - account parameter has been removed and color parameter has been added.
* sendmany - account and minconf parameters have been removed and color parameter has been added.
* listreceivedbyaddress - minconf parameter has been removed.
* listreceivedbylabel - minconf parameter has been removed.
* listtransactions - dummy parameter has been removed.
* listunspent - account has been removed from the result.
* fundrawtransaction - iswitness flag has been removed.

*Removed RPCs*

* getaccountaddress
* getaccount
* getaddressesbyaccount
* movecmd
* sendfrom
* addwitnessaddress
* listaccounts

*Internal codebase*
* Removed witness code from debug mode builds.
* Windows builds using WSL support is available.
