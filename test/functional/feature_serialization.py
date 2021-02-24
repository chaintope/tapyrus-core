#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test serialization of blocks and transactions and signature malleability
Create the same transaction with DER and non-DER signature - check that the txid(hashMalFix) matches

Transaction types included:
    P2PKH
    P2SH(p2sh and redeem)
    P2WPKH
    P2WSH
    p2sh_p2wpkh
    p2sh_p2wsh

This test does the following for every type of transaction:
1) Create a transaction
    get its hash and hashMalFix
    change the signature like Test BIP66 (DER SIG).
    Then check the hash and hashMalFix
    => make sure hash is different but hashMalFix and witness hash remain the same.

2) Check the transaction serialization with different combinations of options with_witness, with_scriptsig:
    +------------------------------+
    | with_witness| with_scriptsig | 
    +==============================+
    |   True, None|    True, None  |

    |   False     |    True, None  |

    |   True, None|    False       |

    |   False     |    False       |
    +------------------------------+

3) Create a block with the non-DER signature transaction created in step 1
    Check the block serialization with with_witness
    Make sure the block is valid and added to the blockchain

"""
from codecs import encode
from io import BytesIO
import struct
from test_framework.blocktools import create_coinbase, create_block, create_transaction, create_tx_with_script, create_witness_tx, add_witness_commitment
from test_framework.messages import msg_block, hash256, CTransaction, ToHex, FromHex, CTxIn, CTxOut, COutPoint
from test_framework.mininode import P2PDataStore, mininode_lock
from test_framework.script import CScript, hash160, OP_1, OP_DROP, OP_HASH160, OP_EQUAL, OP_TRUE, SignatureHash, SIGHASH_ALL
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_raises_rpc_error, assert_equal, bytes_to_hex_str, hex_str_to_bytes, wait_until
from test_framework.key import CECKey

CHAIN_HEIGHT = 11
REJECT_INVALID = 16

def unDERify(tx):
    """
    Make the signature in vin 0 of a tx non-DER-compliant,
    by adding padding after the S-value.
    """
    scriptSig = CScript(tx.vin[0].scriptSig)
    newscript = []
    for i in scriptSig:
        if (len(newscript) == 0):
            try:
                newscript.append(i[0:-1] + b'\0' + i[-1:])
            except TypeError:
                newscript.append(struct.pack("<i", i) + b'\0')
        else:
            newscript.append(i)
    tx.vin[0].scriptSig = CScript(newscript)

def getInput(txid):
    utxo = {}
    utxo["vout"] = 0
    utxo["txid"] = txid
    return utxo

def assert_not_equal(thing1, thing2):
    if thing1 == thing2:
        raise AssertionError("%s == %s" % (str(thing1), str(thing2)))

class SerializationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-whitelist=127.0.0.1']]
        self.setup_clean_chain = True
        self.pubkey = ""

    def run_test(self):
        self.nodes[0].add_p2p_connection(P2PDataStore())
        self.nodeaddress = self.nodes[0].getnewaddress()
        self.pubkey = self.nodes[0].getaddressinfo(self.nodeaddress)["pubkey"]
        self.log.info("Mining %d blocks", CHAIN_HEIGHT)
        self.coinbase_txids = [self.nodes[0].getblock(b)['tx'][0] for b in self.nodes[0].generate(CHAIN_HEIGHT, self.signblockprivkey_wif) ]

        ##  P2PKH transaction
        ########################
        self.log.info("Test using a P2PKH transaction")
        spendtx = create_transaction(self.nodes[0], self.coinbase_txids[0], self.nodeaddress, amount=10)
        spendtx.rehash()
        copy_spendTx = CTransaction(spendtx)

        #cache hashes
        hash = spendtx.hash
        hashMalFix = spendtx.hashMalFix

        #malleate
        unDERify(spendtx)
        spendtx.rehash()
        
        # verify that hashMalFix remains the same even when signature is malleated and hash changes
        assert_not_equal(hash, spendtx.hash)
        assert_equal(hashMalFix, spendtx.hashMalFix)

        # verify that hash is spendtx.serialize()
        hash = encode(hash256(spendtx.serialize())[::-1], 'hex_codec').decode('ascii')
        assert_equal(hash, spendtx.hash)
        
        # verify that hashMalFix is spendtx.serialize(with_scriptsig=False)
        hashMalFix = encode(hash256(spendtx.serialize(with_scriptsig=False))[::-1], 'hex_codec').decode('ascii')
        assert_equal(hashMalFix, spendtx.hashMalFix)

        assert_not_equal(hash, hashMalFix)
        #as this transaction does not have witness data the following is true
        assert_equal(spendtx.serialize(), spendtx.serialize(with_witness=True, with_scriptsig=True))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=True))
        assert_not_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=False))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=True), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=False), spendtx.serialize_without_witness(with_scriptsig=False))

        #Create block with only non-DER signature P2PKH transaction
        tip = self.nodes[0].getbestblockhash()
        block_time = self.nodes[0].getblockheader(tip)['mediantime'] + 1
        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 1), block_time)
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        # serialize with and without witness block remains the same
        assert_equal(block.serialize(with_witness=True), block.serialize())
        assert_equal(block.serialize(with_witness=True), block.serialize(with_witness=False))
        assert_equal(block.serialize(with_witness=True), block.serialize(with_witness=False, with_scriptsig=True))

        self.log.info("Reject block with non-DER signature")
        self.nodes[0].p2p.send_and_ping(msg_block(block))
        assert_equal(self.nodes[0].getbestblockhash(), tip)
        
        wait_until(lambda: "reject" in self.nodes[0].p2p.last_message.keys(), lock=mininode_lock)
        with mininode_lock:
            assert_equal(self.nodes[0].p2p.last_message["reject"].code, REJECT_INVALID)
            assert_equal(self.nodes[0].p2p.last_message["reject"].data, block.sha256)
            assert_equal(self.nodes[0].p2p.last_message["reject"].reason, b'block-validation-failed')

        self.log.info("Accept block with DER signature")
        #recreate block with DER sig transaction
        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 1), block_time)
        block.vtx.append(copy_spendTx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        self.nodes[0].p2p.send_and_ping(msg_block(block))
        assert_equal(self.nodes[0].getbestblockhash(), block.hash)

        ##  P2SH transaction
        ########################
        self.log.info("Test using P2SH transaction ")

        REDEEM_SCRIPT_1 = CScript([OP_1, OP_DROP])
        P2SH_1 = CScript([OP_HASH160, hash160(REDEEM_SCRIPT_1), OP_EQUAL])

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(self.coinbase_txids[1], 16), 0), b"", 0xffffffff))
        tx.vout.append(CTxOut(10, P2SH_1))
        tx.rehash()

        spendtx_raw = self.nodes[0].signrawtransactionwithwallet(ToHex(tx), [], "ALL", self.options.scheme)["hex"]
        spendtx = FromHex(spendtx, spendtx_raw)
        spendtx.rehash()
        copy_spendTx = CTransaction(spendtx)

        #cache hashes
        hash = spendtx.hash
        hashMalFix = spendtx.hashMalFix

        #malleate
        spendtxcopy = spendtx
        unDERify(spendtxcopy)
        spendtxcopy.rehash()
        
        # verify that hashMalFix remains the same even when signature is malleated and hash changes
        assert_not_equal(hash, spendtxcopy.hash)
        assert_equal(hashMalFix, spendtxcopy.hashMalFix)

        # verify that hash is spendtx.serialize()
        hash = encode(hash256(spendtx.serialize(with_witness=False))[::-1], 'hex_codec').decode('ascii')
        assert_equal(hash, spendtx.hash)
        
        # verify that hashMalFix is spendtx.serialize(with_scriptsig=False)
        hashMalFix = encode(hash256(spendtx.serialize(with_witness=False, with_scriptsig=False))[::-1], 'hex_codec').decode('ascii')
        assert_equal(hashMalFix, spendtx.hashMalFix)

        assert_not_equal(hash, hashMalFix)
        #as this transaction does not have witness data the following is true
        assert_equal(spendtx.serialize(), spendtx.serialize(with_witness=True, with_scriptsig=True))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True, with_scriptsig=True))
        assert_not_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=False))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=True), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=False), spendtx.serialize_without_witness(with_scriptsig=False))

        #Create block with only non-DER signature P2SH transaction
        tip = self.nodes[0].getbestblockhash()
        block_time = self.nodes[0].getblockheader(tip)['mediantime'] + 1
        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 2), block_time)
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        # serialize with and without witness block remains the same
        assert_equal(block.serialize(with_witness=True), block.serialize())
        assert_equal(block.serialize(with_witness=True), block.serialize(with_witness=False))
        assert_equal(block.serialize(with_witness=True), block.serialize(with_witness=True, with_scriptsig=True))

        self.log.info("Reject block with non-DER signature")
        self.nodes[0].p2p.send_and_ping(msg_block(block))
        assert_equal(self.nodes[0].getbestblockhash(), tip)
        
        wait_until(lambda: "reject" in self.nodes[0].p2p.last_message.keys(), lock=mininode_lock)
        with mininode_lock:
            assert_equal(self.nodes[0].p2p.last_message["reject"].code, REJECT_INVALID)
            assert_equal(self.nodes[0].p2p.last_message["reject"].data, block.sha256)
            assert_equal(self.nodes[0].p2p.last_message["reject"].reason, b'block-validation-failed')

        self.log.info("Accept block with DER signature")
        #recreate block with DER sig transaction
        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 2), block_time)
        block.vtx.append(copy_spendTx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        self.nodes[0].p2p.send_and_ping(msg_block(block))
        assert_equal(self.nodes[0].getbestblockhash(), block.hash)

        ## redeem previous P2SH
        #########################
        self.log.info("Test using P2SH redeem transaction ")

        tx = CTransaction()
        tx.vout.append(CTxOut(1, CScript([OP_TRUE])))
        tx.vin.append(CTxIn(COutPoint(block.vtx[1].malfixsha256, 0), b''))

        (sighash, err) = SignatureHash(REDEEM_SCRIPT_1, tx, 1, SIGHASH_ALL)
        signKey = CECKey()
        signKey.set_secretbytes(b"horsebattery")
        sig = signKey.sign(sighash) + bytes(bytearray([SIGHASH_ALL]))
        scriptSig = CScript([sig, REDEEM_SCRIPT_1])

        tx.vin[0].scriptSig = scriptSig
        tx.rehash()

        spendtx_raw = self.nodes[0].signrawtransactionwithwallet(ToHex(tx), [], "ALL", self.options.scheme)["hex"]
        spendtx = FromHex(spendtx, spendtx_raw)
        spendtx.rehash()

        #cache hashes
        hash = spendtx.hash
        hashMalFix = spendtx.hashMalFix

        #malleate
        spendtxcopy = spendtx
        unDERify(spendtxcopy)
        spendtxcopy.rehash()
        
        # verify that hashMalFix remains the same even when signature is malleated and hash changes
        assert_not_equal(hash, spendtxcopy.hash)
        assert_equal(hashMalFix, spendtxcopy.hashMalFix)

        # verify that hash is spendtx.serialize()
        hash = encode(hash256(spendtx.serialize(with_witness=False))[::-1], 'hex_codec').decode('ascii')
        assert_equal(hash, spendtx.hash)
        
        # verify that hashMalFix is spendtx.serialize(with_scriptsig=False)
        hashMalFix = encode(hash256(spendtx.serialize(with_witness=False, with_scriptsig=False))[::-1], 'hex_codec').decode('ascii')
        assert_equal(hashMalFix, spendtx.hashMalFix)

        assert_not_equal(hash, hashMalFix)
        #as this transaction does not have witness data the following is true
        assert_equal(spendtx.serialize(), spendtx.serialize(with_witness=True, with_scriptsig=True))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=True))
        assert_not_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=False))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=True), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=False), spendtx.serialize_without_witness(with_scriptsig=False))

        #Create block with only non-DER signature P2SH redeem transaction
        tip = self.nodes[0].getbestblockhash()
        block_time = self.nodes[0].getblockheader(tip)['mediantime'] + 1
        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 3), block_time)
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        # serialize with and without witness block remains the same
        assert_equal(block.serialize(with_witness=True), block.serialize())
        assert_equal(block.serialize(with_witness=True), block.serialize(with_witness=False))
        assert_equal(block.serialize(with_witness=True), block.serialize(with_witness=True, with_scriptsig=True))

        self.log.info("Accept block with P2SH redeem transaction")
        self.nodes[0].p2p.send_and_ping(msg_block(block))
        assert_equal(self.nodes[0].getbestblockhash(), block.hash)

        ##  p2sh_p2wpkh transaction
        ##############################
        self.log.info("Test using p2sh_p2wpkh transaction ")
        spendtxStr = create_witness_tx(self.nodes[0], True, getInput(self.coinbase_txids[4]), self.pubkey, amount=1.0)
        
        #get CTRansaction object from above hex
        spendtx = CTransaction()
        spendtx.deserialize(BytesIO(hex_str_to_bytes(spendtxStr)))
        spendtx.rehash()

        #cache hashes
        spendtx.rehash()
        hash = spendtx.hash
        hashMalFix = spendtx.hashMalFix
        withash = spendtx.calc_sha256(True)

        # malleate
        unDERify(spendtx)
        spendtx.rehash()
        withash2 = spendtx.calc_sha256(True)
        
        # verify that hashMalFix remains the same even when signature is malleated and hash changes
        assert_equal(withash, withash2)
        assert_equal(hash, spendtx.hash)
        assert_equal(hashMalFix, spendtx.hashMalFix)

        # verify that hash is spendtx.serialize()
        hash = encode(hash256(spendtx.serialize())[::-1], 'hex_codec').decode('ascii')
        assert_equal(hash, spendtx.hash)
        
        # verify that hashMalFix is spendtx.serialize(with_scriptsig=False)
        hashMalFix = encode(hash256(spendtx.serialize(with_scriptsig=False))[::-1], 'hex_codec').decode('ascii')
        assert_equal(hashMalFix, spendtx.hashMalFix)

        assert_not_equal(hash, hashMalFix)
        #as this transaction does not have witness data the following is true
        assert_equal(spendtx.serialize(), spendtx.serialize(with_witness=True, with_scriptsig=True))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=True))
        assert_not_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=False))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=True), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=False), spendtx.serialize_without_witness(with_scriptsig=False))

        #Create block with only non-DER signature p2sh_p2wpkh transaction
        spendtxStr = self.nodes[0].signrawtransactionwithwallet(spendtxStr, [], "ALL", self.options.scheme)
        assert("errors" not in spendtxStr or len(["errors"]) == 0)
        spendtxStr = spendtxStr["hex"]
        spendtx = CTransaction()
        spendtx.deserialize(BytesIO(hex_str_to_bytes(spendtxStr)))
        spendtx.rehash()

        tip = self.nodes[0].getbestblockhash()
        block_time = self.nodes[0].getblockheader(tip)['mediantime'] + 1
        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 4), block_time)
        block.vtx.append(spendtx)
        add_witness_commitment(block)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        # serialize with and without witness
        assert_equal(block.serialize(with_witness=False), block.serialize())
        assert_not_equal(block.serialize(with_witness=True), block.serialize(with_witness=False))
        assert_not_equal(block.serialize(with_witness=True), block.serialize(with_witness=False, with_scriptsig=True))

        self.log.info("Reject block with p2sh_p2wpkh transaction and witness commitment")
        assert_raises_rpc_error(-22, "Block does not start with a coinbase", self.nodes[0].submitblock, bytes_to_hex_str(block.serialize(with_witness=True)))
        assert_equal(self.nodes[0].getbestblockhash(), tip)

        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 4), block_time)
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        self.log.info("Accept block with p2sh_p2wpkh transaction")
        self.nodes[0].submitblock(bytes_to_hex_str(block.serialize(with_witness=True)))
        assert_equal(self.nodes[0].getbestblockhash(), block.hash)

        ##  p2sh_p2wsh transaction
        ##############################
        self.log.info("Test using p2sh_p2wsh transaction")
        spendtxStr = create_witness_tx(self.nodes[0], True, getInput(self.coinbase_txids[5]), self.pubkey, amount=1.0)

        #get CTRansaction object from above hex
        spendtx = CTransaction()
        spendtx.deserialize(BytesIO(hex_str_to_bytes(spendtxStr)))
        spendtx.rehash()

        #cache hashes
        spendtx.rehash()
        hash = spendtx.hash
        hashMalFix = spendtx.hashMalFix
        withash = spendtx.calc_sha256(True)

        # malleate
        unDERify(spendtx)
        spendtx.rehash()
        withash2 = spendtx.calc_sha256(True)
        
        # verify that hashMalFix remains the same even when signature is malleated and hash changes
        assert_equal(withash, withash2)
        assert_equal(hash, spendtx.hash)
        assert_equal(hashMalFix, spendtx.hashMalFix)

        # verify that hash is spendtx.serialize()
        hash = encode(hash256(spendtx.serialize())[::-1], 'hex_codec').decode('ascii')
        assert_equal(hash, spendtx.hash)
        
        # verify that hashMalFix is spendtx.serialize(with_scriptsig=False)
        hashMalFix = encode(hash256(spendtx.serialize(with_scriptsig=False))[::-1], 'hex_codec').decode('ascii')
        assert_equal(hashMalFix, spendtx.hashMalFix)

        assert_not_equal(hash, hashMalFix)
        #as this transaction does not have witness data the following is true
        assert_equal(spendtx.serialize(), spendtx.serialize(with_witness=True, with_scriptsig=True))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=True))
        assert_not_equal(spendtx.serialize(with_witness=False), spendtx.serialize(with_witness=True,with_scriptsig=False))
        assert_equal(spendtx.serialize(with_witness=False), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=True), spendtx.serialize_without_witness(with_scriptsig=True))
        assert_equal(spendtx.serialize_with_witness(with_scriptsig=False), spendtx.serialize_without_witness(with_scriptsig=False))

        #Create block with only non-DER signature p2sh_p2wsh transaction
        spendtxStr = self.nodes[0].signrawtransactionwithwallet(spendtxStr, [], "ALL", self.options.scheme)
        assert("errors" not in spendtxStr or len(["errors"]) == 0)
        spendtxStr = spendtxStr["hex"]
        spendtx = CTransaction()
        spendtx.deserialize(BytesIO(hex_str_to_bytes(spendtxStr)))
        spendtx.rehash()

        tip = self.nodes[0].getbestblockhash()
        block_time = self.nodes[0].getblockheader(tip)['mediantime'] + 1
        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 5), block_time)
        block.vtx.append(spendtx)
        add_witness_commitment(block)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        # serialize with and without witness
        assert_equal(block.serialize(with_witness=False), block.serialize())
        assert_not_equal(block.serialize(with_witness=True), block.serialize(with_witness=False))
        assert_not_equal(block.serialize(with_witness=True), block.serialize(with_witness=False, with_scriptsig=True))

        self.log.info("Reject block with p2sh_p2wsh transaction and witness commitment")
        assert_raises_rpc_error(-22, "Block does not start with a coinbase", self.nodes[0].submitblock, bytes_to_hex_str(block.serialize(with_witness=True)))
        assert_equal(self.nodes[0].getbestblockhash(), tip)

        block = create_block(int(tip, 16), create_coinbase(CHAIN_HEIGHT + 5), block_time)
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.rehash()
        block.solve(self.signblockprivkey)

        self.log.info("Accept block with p2sh_p2wsh transaction")
        self.nodes[0].submitblock(bytes_to_hex_str(block.serialize(with_witness=True)))
        assert_equal(self.nodes[0].getbestblockhash(), block.hash)

if __name__ == '__main__':
    SerializationTest().main()
