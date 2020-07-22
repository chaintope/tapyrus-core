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

In a transaction, the total amount of tokens in the input color identifier and the total amount of tokens in the output color identifier shall be the same.
That is, it maintains the balance of each token in the transaction. Please note that Signer does not receive any tokens (except default token) as a fee for block generation at this stage.

If you use `OP_COLOR` in Script, the following restrictions are applied.

* Only one `OP_COLOR` can be included in a script.
* A `OP_COLOR` cannot be written in a control opcode such as `OP_IF`.

Because of the above counts and constraints, the scriptPubkey of the custom token of each output always contains `OP_COLOR`.
Therefore, it is not possible to include `OP_COLOR` in the redeem script of P2SH. 
When such a script is configured, the The stack of the script interpreter contains multiple `OP_COLOR`, 
which always leads to an error and loss of coins when using P2SH.

As a result, we support the following types of scriptPubkey that coloring the existing P2PKH and P2SH.

* `CP2PKH`(Colored P2PKH)：  
`<COLOR identifier> OP_COLOR OP_DUP OP_HASH160 <H(pubkey)> OP_EQUALVERIFY OP_CHECKSIG`
* `CP2SH`(Colored P2SH)：  
`<COLOR identifier> OP_COLOR OP_HASH160 <H(redeem script)> OP_EQUAL`

Since Tapyrus Core allows arbitrary scripts by the standard rule of transaction, 
you can use `OP_COLOR` with arbitrary scripts other than the above.

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

## Issue Token

When a token is newly issued, a UTXO for issuing the token shall be set to the input and a COLOR identifier shall be derived from the UTXO based on the above rules.
Create a transaction with scriptPubkey(CP2PKH, CP2SH, etc) using the COLOR identifier and `OP_COLOR` opcode in the transaction output.

The output of issuing a new token can be set to an arbitrary amount of tokens with `value`.
In addition, the UTXO TPC specified in the input creates and recovers another non-token output(like P2PKH and P2SH). `

## Transfer Token

To transfer a token, create a transaction with a Token UTXO as the input, 
and add the output with the same COLOR identifier as the input token and the `OP_COLOR` opcode to the destination address.

The number of inputs, the number of outputs, and the type of tokens can be set, and it does not matter if the total amount of each type of token is maintained in the inputs and outputs.

## Burn Token

To burn a token, create a transaction with a UTXO with the token to be burned and a UTXO with a TPC for the fee as inputs, 
and add an output that receives TPC change. 
Since the value of a UTXO for a token is entirely the amount of the token, in order to set the fees, it is necessary to set the UTXO of TPC. 
Also, do not use `OP_COLOR` opcode in the scriptPubkey of the output to burn it.

It is also possible to combine the above three token processes into a single transaction.

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

# References

* [OP_GROUP](https://github.com/gandrewstone/BitcoinUnlimited/blob/238ca764385f94a4c371e61424e3307d7da9eb56/doc/opgroup-tokens.md)
* [On Representative Tokens (Colored Coins)](https://www.yours.org/content/on-representative-tokens--colored-coins--bb7a829b965c/)
* [Response to OP_GROUP Criticism](https://www.yours.org/content/response-to-op_group-criticism-d088a7f1e6ad)