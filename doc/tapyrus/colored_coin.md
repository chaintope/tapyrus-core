This document describes the specification for supporting tokens other than TPC (tapyrus) on Tapyrus.

# Introduction

We introduce a new opcode`OP_COLOR`(0xbc) to allow Tapyrus to issue/transfer/burn arbitrary tokens.
`OP_COLOR` is based on `OP_GROUP`, which was proposed in BCH before, and added some improvements.

It enables the following features:

* Issue Token
   * Issuing reissuable tokens
   * Issuing non-reissuable tokens
   * Issuing the NFT(Non Fungible Token)
* Transfer Token
* Burn token

# Specification

When `OP_COLOR` opcode appears in a script, the top element of the stack is popped which interpreted as a `COLOR identifier`.
The script will fail if there are no elements left in the stack, or if the COLOR identifier does not matched the rules described below.
If the COLOR identifier matches to the rule, the coin of the UTXO represents the amount of the coin for the COLOR identifier.
Script which does not include `OP_COLOR` represents to the amount of the default token TPC (tapyrus).

In a transaction, the total amount of tokens in the outputs for a given color identifier must not exceed the total amount of tokens in the inputs for that same color identifier. Any surplus — inputs that exceed outputs for a color — is permanently destroyed. Please note that Signer does not receive any tokens (except default token) as a fee for block generation at this stage.

For outputs whose scriptPubKey contains `OP_COLOR`, the `nValue` field carries the quantity of the named token as a raw integer, not satoshis. For outputs whose scriptPubKey does not contain `OP_COLOR`, `nValue` carries TPC in satoshis. Token `nValue` is not counted toward TPC transaction fees. Accordingly, every token transaction is required to include at least one TPC-denominated input to cover the fee; a transaction that contains only colored inputs is rejected with `bad-txns-token-without-fee`.

If you use `OP_COLOR` in Script, the following restrictions are applied.

* Only one `OP_COLOR` is allowed in a scriptPubKey.
* The scriptPubKey of a colored output must match exactly one of the templates listed below (CP2PKH / CP2SH). Other opcodes (`OP_IF`, `OP_CHECKLOCKTIMEVERIFY`, `OP_CHECKSEQUENCEVERIFY`, `OP_SWAP`, `OP_DROP`, etc.) are not permitted in the scriptPubKey of a colored output.
* These opcodes may freely appear inside a CP2SH redeem script, which is evaluated only at spend time and does not contain `OP_COLOR`.

As a result, we support the following types of scriptPubkey that coloring the existing P2PKH and P2SH.

* `CP2PKH`(Colored P2PKH)：  
`<COLOR identifier> OP_COLOR OP_DUP OP_HASH160 <H(pubkey)> OP_EQUALVERIFY OP_CHECKSIG`
* `CP2SH`(Colored P2SH)：  
`<COLOR identifier> OP_COLOR OP_HASH160 <H(redeem script)> OP_EQUAL`

**A concensus change is being mandated from 0.7.2.**:   Tapyrus Core now allows only the above two scripts to use `OP_COLOR`. Custom `OP_COLOR` scripts are **not** allowed in order to prevent vulnerabilities arising due to mischeviously constructed scripts that could bypass consensus rules.


## Color identifier

A COLOR identifier consists of one byte of `TYPE` and 32 bytes of `PAYLOAD`.

The types currently supported by the COLOR identifier and the corresponding payloads are as follows:

Type|Description|Payload
---|---|---
0xC1|reissuable token|32 bytes of data that is the SHA256 value of the scriptPubkey in the issue input.
0xC2|non-reissuable tokens|32 bytes of data that is the SHA256 value of the OutPoints at issue input.
0xC3|NFT|32 bytes of data that is the SHA256 value of the OutPoints at issue input.

In Tx of issuing a token, we verify whether the input and outputs satisfies the above rule.
In addition, in the case of type `0xC3`, we must verify that the amount issued is 1 value.

For type `0xC1`, the payload is computed as `SHA256(scriptPubKey of the designated input)`. Because the payload depends only on the scriptPubKey and not on the outpoint, any TPC UTXO whose scriptPubKey produces the same hash may be used to issue additional tokens of the same color in a future transaction.
For types `0xC2` and `0xC3`, the payload is computed as `SHA256(serialized COutPoint of the designated input)`. Because each outpoint is consumed exactly once, the resulting color identifier is globally unique and cannot be reproduced in any subsequent transaction.

On issuance, the following checks are applied by `CheckColorIdentifierValidity`:

* For each colored output, a TPC input is located and the expected color identifier is derived from it. If the output type is `0xC1`, the expected identifier is recomputed as `SHA256(scriptPubKey of that input)`. If the type is `0xC2` or `0xC3`, the expected identifier is recomputed as `SHA256(serialized COutPoint of that input)`. A mismatch causes the transaction to be rejected.
* Token outputs with `nValue ≤ 0` are rejected.
* For type `0xC3`, the output `nValue` must be exactly `1`; any other value is rejected.
* For types `0xC2` and `0xC3`, the color identifier is checked against the global set of previously issued identifiers; a duplicate is rejected as `bad-txns-colorid-already-issued`.

## Issue Token

When a token is newly issued, a UTXO for issuing the token shall be set to the input and a COLOR identifier shall be derived from the UTXO based on the above rules.
Create a transaction with scriptPubkey(CP2PKH or CP2SH) using the COLOR identifier and `OP_COLOR` opcode in the transaction output.

The output of issuing a new token can be set to an arbitrary amount of tokens with `value`.
In addition, the UTXO TPC specified in the input creates and recovers another non-token output(like P2PKH and P2SH). `

## Transfer Token

To transfer a token, create a transaction with a Token UTXO as the input, 
and add the output with the same COLOR identifier as the input token and the `OP_COLOR` opcode to the destination address.

The number of inputs, the number of outputs, and the type of tokens can be set, and it does not matter if the total amount of each type of token is maintained in the inputs and outputs.

Token balance is enforced independently per color identifier. For each color identifier present in the transaction outputs, the sum of output `nValue` for that identifier must not exceed the sum of input `nValue` for the same identifier. Each color identifier is accounted separately; a surplus or deficit in one color does not offset another. A transaction carrying outputs of multiple distinct color identifiers is valid provided each color's balance constraint is satisfied individually.

## Burn Token

To burn a token, create a transaction with a UTXO with the token to be burned and a UTXO with a TPC for the fee as inputs, 
and add an output that receives TPC change. 
Since the value of a UTXO for a token is entirely the amount of the token, in order to set the fees, it is necessary to set the UTXO of TPC.

It is also possible to combine the above three token processes into a single transaction.

Each combination and valid/invalid pattern described in the following:

* https://docs.google.com/spreadsheets/d/1hYEe5YVz5NiMzBD2cTYLWdOEPVUkTmVIRp8ytypdr2g/

## Address

Define new address formats for CP2PKH and CP2SH.

### Version bytes

The version bytes used for address generation are as follows:

Type|prod|dev
---|---|---
CP2PKH|0x01|0x70
CP2SH|0x06|0xc5

### Payload

P2PKH and P2SH used the RIPEMD-160 hash value of the public key and the redeem script as the address payload, but the CP2PKH and CP2SH addresses are composed of the following data (| indicates data concat).

* CP2PKH: `<Color Identifier(33 bytes)>|<Pubkey Hash(20 bytes)>`
* CP2SH: `<Color Identifier(33 bytes)>|<Script Hash(20 bytes)>`

In both cases, the payload data length is 53 bytes.

### Notes

Unlike the P2PKH address and P2SH address, the CP2PKH address and CP2SH address cannot be created without knowing the Color identifier. However, the user who wants to receive the colored coin does not necessarily know the identifier. It is conceivable that the user receiving the coin will present the P2PKH address or P2SH address, and the sender will give it a color identifier and send it. Therefore, CP2PKH address and CP2SH address will be used only in the wallet owned by itself.

# Concerns

* The scope of OP_COLOR's influence is not confined to the script, but has an effect on transaction inputs/outputs in the sense of quantity checks.
* In the case of NFT, it is necessary to ensure that the token is unique throughout the blockchain requiring additional validation. Uniqueness is enforced by two mechanisms. First, the color identifier payload is `SHA256(serialized COutPoint)`, and an outpoint is consumed exactly once, so the identifier can be produced in only one transaction. Second, all issued `0xC2` and `0xC3` color identifiers are recorded in a global set; any attempt to issue an already-recorded identifier is rejected as `bad-txns-colorid-already-issued`. Additionally, a transaction that contains more than one output sharing the same NFT color identifier is rejected as `bad-txns-nft-output-count`.

## Upgrade Warning

Tapyrus Core versions upto 0.7.1 allowed other arbitrary scripts with `<COLOR identifier> OP_COLOR` pattern. From Tapyrus Core version 0.7.2 only CP2PKH and CP2SH scripts with `OP_COLOR` are allowed in the scriptpubkey. This restriction has been mandated in order to prevent vulnerabilities arising due to mischeviously constructed scripts. All other arbitrary scripts with OP_COLOR are rejected by the v0.7.2 node. **So any Tapyrus core network and blockchain that allowed transactions with custom `OP_COLOR` scripts would see block rejection when upgrading to v0.7.2.** It is essential that such networks plan their upgrade with utmost caution. It would be absolutely necessary to identify all the transactions having custom OP_COLOR scripts. Create alternate transactions by changing the scripts. Note that this would change the trasnaction id and as a result the block hash too. So invalidate the old block which is rejected by the upgraded node. A new block needs to be generated at the rejected block height. Thus a complete blockchain reorg from the failed block onwards would be absolutely necessary in order to upgrade. _**It is recommended that the utxo set and transaction list are audited and backed up before the upgrade in order to verify that no transaction has been lost and the utxo set is consistent after the upgrade.**_

Note that this upgrade warning affects only a network with a blockchain that has blocks with transactions using custom OP_COLOR scripts. All other networks that use the standard CP2SH and CP2PKH scripts are not affected by this consensus change.

# References

* [OP_GROUP](https://github.com/gandrewstone/BitcoinUnlimited/blob/238ca764385f94a4c371e61424e3307d7da9eb56/doc/opgroup-tokens.md)
* [On Representative Tokens (Colored Coins)](https://www.yours.org/content/on-representative-tokens--colored-coins--bb7a829b965c/)
* [Response to OP_GROUP Criticism](https://www.yours.org/content/response-to-op_group-criticism-d088a7f1e6ad)