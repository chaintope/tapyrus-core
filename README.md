Tapyrus Core [![Build Status](https://github.com/chaintope/tapyrus-core/workflows/CI%20on%20Push/badge.svg?branch=master)](https://github.com/chaintope/tapyrus-core/actions?query=workflow%3A%22CI+on+Push%22)
=====================================

![tapyrus](doc/images/tapyrus-logo.png)

What is Tapyrus?
----------------

Tapyrus is a blockchain that has been forked from Bitcoin Core, and solves the governance issues.
Bitcoin uses Proof of Work as a consensus algorithm, but Tapyrus creates a block (ie, approves a transaction)
with multiple signatures from the Signer group which consists of industry stakeholders.
Therefore, transactions on the blockchain are stably approved, and finality is given at the time of approval.
Anyone can join the Tapyrus network, and anyone can access the public ledger and create and broadcast transactions freely.

Tapyrus has the following additions and changes to the Bitcoin implementation.:

* [Support Schnorr Signature](https://github.com/chaintope/tapyrus-core/tree/master/doc/tapyrus/schnorr_signature.md)
* [Fix transaction malleability](https://github.com/chaintope/tapyrus-core/tree/master/doc/tapyrus/fix_transaction_malleability.md)
that enables off-chain payments using the Payment Channel on Tapyrus.
* [Support Oracle contract](https://github.com/chaintope/tapyrus-core/tree/master/doc/tapyrus/script.md)
* [Block generation with multiple signatures](https://github.com/chaintope/tapyrus-core/tree/master/doc/tapyrus/signedblocks.md)
* [WIP]Support native colored coin protocol
* [WIP]Support aggregated/threshold signatures
* [WIP]Support covenants

Tapyrus consists of the following software:

* [Tapyrus Core](https://github.com/chaintope/tapyrus-core): Tapyrus full node implementation.
* [Tapyrus Signer](https://github.com/chaintope/tapyrus-signer): A network of signers that collects Unapproved transactions from the Tapyrus network and creates a block.
* [Tapyrus Seeder](https://github.com/chaintope/tapyrus-seeder): DNS seeder for configuring Tapyrus network.
* [[WIP]Tapyrus SPV](https://github.com/chaintope/tapyrus-spv): A lightweight client implementation for Tapyrus blockchain.
* [[WIP]Tapyrus Explorer](https://github.com/chaintope/tapyrus-explorer): Tapyrus blockchain explorer.
* [[WIP]Electrs Tapyrus](https://github.com/chaintope/electrs-tapyrus): Tapyrus blockchain index server.

License
-------

Tapyrus Core is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

