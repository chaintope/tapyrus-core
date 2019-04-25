Bitcoin transaction 
===================
Recall the bitcoin transactions structure:

*|version|number of inputs|input{outpoint{hash, index}, ScriptSig, sequence}|number of outputs|output{value, scriptPubKey}|LockTime|*

In bitcoin Transaction Id is the SHA256 hash computed on the serialized form of the above sequence, meaning that the length of each field is appended before it for hashing. Witness transactions in Bitcoin also adds a [marker][flag] to the transaction and a witnessScript field to every input. This are not hashed to get the transaction Id. 

Transaction Malleability
========================
The primary cause of transaction malleability in bitcoin is that transaction id is not unique. A transaction could be malleated without invalidating it by changing the ScriptSig as it is not signed.

An alternate approach to using Segregated witness for fixing transaction malleability in bitcoin was proposed by Tomas van der Wansem - https://github.com/tomasvdw/bips/blob/master/malleability-fix.mediawiki

The idea essentially is to exclude scriptSig while hashing for transaction id - Immutable transaction Id. This fixes the primary cause of transaction malleability as immutable transaction id represents only the inputs and outputs. The implementation in Tapyrus is simpler than suggested by the BIP proposal above, as we do not have to support both versions of transaction Id.

Immutable Transaction Id
========================
In Tapyrus all transaction IDs are Immutable Transaction IDs - *(hashMalFix in the code)*. Like the regular transaction Id in bitcoin, Immutable transactionId computation is in CTransaction and CMutableTransaction classes in Tapyrus. 

All layers of Tapyrus use Immutable transaction id.
. P2P Network (INV, TX) messages and relay pool
. Memory pool and transaction validation
. Coin cache level DB
. Wallet

Tapyrus transaction 
===================
As Immutable transaction Id is used everywhere, Tapyrus transactions refer to the previous output using their Immutable transaction Id.

*|version|number of inputs|input{outpoint{**hashMalFix**, index}, ScriptSig, sequence, **scriptWitness**}|number of outputs|output{value, scriptPubKey}|LockTime|*

There are 3 types of hashes in Tapyrus transactions:
|Name of Hash |Data omited while hashing  |Usage in Tapyrus           |
|hash         |scriptWitness              |Merkle root                |
|hashMalFix   |sIcriptSig, scriptWitness  |Immutable transaction Id, Immutable Merkle root   |
|hashWitness  |-                          |Witness block    |

Tapyrus Coinase transactions
----------------------------
 Recall that coinbase transactions have only one input whose outPoint is null (hash is  (0) and index is -1). Bitcoin encountered genuine duplicate transaction ids because coinbase transactions could easily be duplicated. To fix this, [BIP-34](https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki) added block height as the first field of scriptSig of coinbase transactions. In Tapyrus the same technique is applied differently - block height is added as the index field of the outPoint in the input.

**Tapyrus coinbase transaction**

|version|number of inputs|input:outpoint(hashMalFix, index), ScriptSig, sequence|number of outputs|output:(amount script)|
*|00000000|01|0000000000000000000000000000000000000000000000000000000000000000**00000001**0000000000|01|...*

**Merkle root**

Merkle root in Tapyrus commits to all the data in the block like bitcoin, so it includes scriptSig. 

**Immutable Merkle root**

Since Merkle root in Tapyrus includes ScriptSig it becomes difficult for wallets with bloom filters to identify whether a transaction was included in a block using its Merkle root. So an additional Merkle root is added - this is computed using immutable transaction Id. This allows Tapyrus to use featues like Bloom Filters based on Immutable transacton Id. 

**Compact blocks**

As a consequence of this change Compact blocks in Tapyrus also use Immutable transaction Id regardless of the compact block version requested. In bitcoin compact block version 1 is non-witness and version 2 is witness. In Tapyrus Witness transaction id used only in transactions that have witness data even when compact block version requested is 2.


In all RPCs 'txid' is always the Immutable Transaction Id. This table summarises RPCs affected by Immutable Transaction Id(hashMalFix), their inputs and outputs

| RPC |Input  | Output |
| :---: | :---: | :---: | 
| **getrawtransaction |Argument 1 is hashMalFix(txId)  string | Result['txid'] is hashMalFix | 
|gettxoutproof|Argument 1 is txids - hashMalFix(txId) array|N/A|
|verifytxoutproof|N/A|array of txids - hashMalFix(txId) |
|createrawtransaction|Argument 1 - inputs[{txid}] is hashMalFix(txId) of previous transaction |N/A|
|**decoderawtransaction|N/A|txid and vin['txid'] are both hashMalFix(txid)|
|signrawtransactionwithkey|Argument 3 - prevtxs['txid']  is hashMalFix(txId) of previous transaction |errors['txid']|
|signrawtransaction|Argument 2 - prevtxs['txid']  is hashMalFix(txId) of previous transaction |errors['txid']|
|signrawtransactionwithwallet|Argument 2 - prevtxs['txid']  is hashMalFix(txId) of previous transaction |errors['txid']|
|sendrawtransaction|N/A|'hex' is hashMalFix(txId) in hex|
|testmempoolaccept|N/A|Result['txid'] is hashMalFix(txId) in hex|
|createpsbt, walletcreatefundedpsbt |Argument 1- inputs['txid'] is hashMalFix(txId) in hex|N/A|
|prioritisetransaction|Argument 1- 'txid' is hashMalFix(txId) |N/A|
|getblocktemplate|N/A|transactions['txid] is hashMalFix(txId)|
|getrawmempool|N/A|'transactionid' is hashMalFix(txId)|
|getmempoolancestors|Argument 1- 'txid' is hashMalFix(txId) |'transactionid' is hashMalFix(txId) of in-mempool ancestor transaction|
|getmempooldescendants|Argument 1- 'txid' is hashMalFix(txId)|'transactionid' is hashMalFix(txId) of in-mempool descendant transaction|
|getmempoolentry|Argument 1- 'txid' is hashMalFix(txId)|N/A|
|getblock|N/A|tx['transactionid' is hashMalFix(txId) |
|gettxout|Argument 1- 'txid' is hashMalFix(txId)|N/A|
|scantxoutset|N/A|unspents['txid'] is hashMalFix(txId)|
|removeprunedfunds|Argument 1- 'txid' is hashMalFix(txId)|N/A|
|listtransactions|N/A|Result['txid'] is hashMalFix(txId)|
|listsinceblock|N/A|transactions['txid'] is hashMalFix(txId)|
|gettransaction|Argument 1- 'txid' is hashMalFix(txId)|Result['txid'] is hashMalFix(txId)|
|lockunspent|Argument 2 - transactions['txid'] is hashMalFix(txId)|N/A|
|listlockunspent|N/A|Argument 2 - transactions['txid'] is hashMalFix(txId)|
|listunspent|N/A|Result['txid'] is hashMalFix(txId)|
|bumpfee|Argument 1- 'txid' is hashMalFix(txId)|Result['txid'] is hashMalFix(txId)|
git 

** Result['hash'] is the transaction hash including scriptSig. This is different from 'txid'. In Witness transactions Result['hash'] is the witness hash.