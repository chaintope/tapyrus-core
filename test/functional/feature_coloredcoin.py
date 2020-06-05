'''
Test all token type - REISSUABLE, NON-REISSUABLE, NFT

Setup:
coinbaseTx: 1 - 10

Create TXs:
TxSuccess1 - coinbaseTx1 - issue 100 REISSUABLE  + 30     (UTXO-1,2)
TxSuccess2 - (UTXO-2)    - issue 100 NON-REISSUABLE       (UTXO-3)
TxSuccess3 - coinbaseTx2 - issue 1 NFT                    (UTXO-4)

TxSuccess4 - (UTXO-1)    - split REISSUABLE - 25 + 75     (UTXO-5,6)
           - (UTXO-3)    - split NON-REISSUABLE - 40 + 60 (UTXO-7,8)
           - coinbaseTx3 - issue 100 REISSUABLE           (UTXO-9)

TxSuccess5 - (UTXO-6)    - split REISSUABLE(75)           (UTXO-10,11)
           - (UTXO-7)    - split NON-REISSUABLE(40)       (UTXO-12)
           - (UTXO-4)    - split NFT                      (UTXO-13)
           - coinbaseTx4

TxSuccess6 - (UTXO-11)   - transfer REISSUABLE(40)        (UTXO-14)
           - (UTXO-8)    - burn NON-REISSUABLE(60)        (UTXO-15)*
           - (UTXO-13)   - transfer NFT                   (UTXO-16)
           - coinbaseTx5 - issue 1000 REISSUABLE1, change (UTXO-17)

txFailure1 - (UTXO-9,14) - aggregate REISSUABLE(40 + 100) x
           - (UTXO-7)    - burn NON-REISSUABLE(40)        x
           - (UTXO-4)    - transfer NFT                   x
           - change

txFailure2 - (UTXO-15)   - convert REISSUABLE to NON-REISSUABLE      x

'''

import shutil, os
import time

from io import BytesIO

from test_framework.blocktools import create_block, create_coinbase, create_tx_with_script, create_transaction
from test_framework.messages import CTransaction, CTxIn, CTxOut, COutPoint, msg_tx, COIN, sha256, msg_block
from test_framework.key import CECKey
from test_framework.schnorr import Schnorr
from test_framework.mininode import P2PDataStore, mininode_lock
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes, bytes_to_hex_str, assert_raises_rpc_error, NetworkDirName
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_1, OP_COLOR, hash160, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG, SignatureHash, SIGHASH_ALL

def colorIdReissuable(script):
    return b'\x01' + sha256(script)

def colorIdNonReissuable(utxo):
    return b'\x02'+ sha256(utxo)

def colorIdNFT(utxo):
    return b'\x03'+ sha256(utxo)

def CP2PHK_script(colorId, pubkey):
    pubkeyhash = hash160(hex_str_to_bytes(pubkey))
    return CScript([colorId, OP_COLOR, OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ])

def CP2SH_script(colorId, redeemScr):
    redeemScrhash = hash160(hex_str_to_bytes(redeemScr))
    return CScript([colorId, OP_COLOR, OP_HASH160, redeemScrhash, OP_EQUAL])

def test_transaction_acceptance(node, tx, accepted, reason=None):
    """Send a transaction to the node and check that it's accepted to the mempool"""
    with mininode_lock:
        node.p2p.last_message = {}
    tx_message = msg_tx(tx)
    node.p2p.send_message(tx_message)
    node.p2p.sync_with_ping()
    assert_equal(tx.hashMalFix in node.getrawmempool(), accepted)
    if (reason is not None and not accepted):
        # Check the rejection reason as well.
        with mininode_lock:
            assert_equal(node.p2p.last_message["reject"].reason, reason)

class ColoredCoinTest(BitcoinTestFramework):
    def set_test_params(self):
        self.pubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc"]
        
        privkeystr = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37",
        "dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b"]

        self.privkeys = []
        for key in privkeystr :
            ckey = CECKey()
            ckey.set_secretbytes(bytes.fromhex(key))
            self.privkeys.append(ckey)

        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()

        self.schnorr_key = Schnorr()
        self.schnorr_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))

        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]  # convenience reference to the node
        self.address = node.getnewaddress()
        node.add_p2p_connection(P2PDataStore())
        node.p2p.wait_for_getheaders(timeout=5)
        self.address = self.nodes[0].getnewaddress()

        self.log.info("Test starting...")

        #generate 10 blocks for coinbase outputs
        coinbase_txs = []
        for i in range(1, 10):
            height = node.getblockcount() + 1
            coinbase_tx = create_coinbase(height, self.coinbase_pubkey)
            coinbase_txs.append(coinbase_tx)
            tip = node.getbestblockhash()
            block_time = node.getblockheader(tip)["mediantime"] + 1
            block = create_block(int(tip, 16), coinbase_tx, block_time)
            block.solve(self.signblockprivkey)
            tip = block.hash

            node.p2p.send_and_ping(msg_block(block))
            assert_equal(node.getbestblockhash(), tip)

        change_script = CScript([self.coinbase_pubkey, OP_CHECKSIG])
        burn_script = CScript([hex_str_to_bytes(self.pubkeys[1]), OP_CHECKSIG])

        #TxSuccess1 - coinbaseTx1 - issue 100 REISSUABLE  + 30     (UTXO-1,2)
        colorId_reissuable = colorIdReissuable(coinbase_txs[0].vout[0].scriptPubKey)
        script_reissuable = CP2PHK_script(colorId = colorId_reissuable, pubkey = self.pubkeys[0])
        script_transfer_reissuable = CP2PHK_script(colorId = colorId_reissuable, pubkey = self.pubkeys[1])

        txSuccess1 = CTransaction()
        txSuccess1.vin.append(CTxIn(COutPoint(coinbase_txs[0].malfixsha256, 0), b""))
        txSuccess1.vout.append(CTxOut(100, script_reissuable))
        txSuccess1.vout.append(CTxOut(30 * COIN, CScript([self.coinbase_pubkey, OP_CHECKSIG])))
        sig_hash, err = SignatureHash(coinbase_txs[0].vout[0].scriptPubKey, txSuccess1, 0, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'  # 0x1 is SIGHASH_ALL
        txSuccess1.vin[0].scriptSig = CScript([signature])
        txSuccess1.rehash()

        test_transaction_acceptance(node, txSuccess1, accepted=True)

        #TxSuccess2 - (UTXO-2)    - issue 100 NON-REISSUABLE       (UTXO-3)
        colorId_nonreissuable = colorIdNonReissuable(COutPoint(txSuccess1.malfixsha256, 1).serialize())
        script_nonreissuable = CP2PHK_script(colorId = colorId_nonreissuable, pubkey = self.pubkeys[0])
        script_transfer_nonreissuable = CP2PHK_script(colorId = colorId_nonreissuable, pubkey = self.pubkeys[1])

        txSuccess2 = CTransaction()
        txSuccess2.vin.append(CTxIn(COutPoint(txSuccess1.malfixsha256, 1), b""))
        txSuccess2.vout.append(CTxOut(100, script_nonreissuable))
        sig_hash, err = SignatureHash(txSuccess1.vout[1].scriptPubKey, txSuccess2, 0, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess2.vin[0].scriptSig = CScript([signature])
        txSuccess2.rehash()

        test_transaction_acceptance(node, txSuccess2, accepted=True)

        #TxSuccess3 - coinbaseTx2 - issue 1 NFT                    (UTXO-4)
        colorId_nft = colorIdNFT(COutPoint(coinbase_txs[1].malfixsha256, 0).serialize())
        script_nft = CP2PHK_script(colorId = colorId_nft, pubkey = self.pubkeys[0])
        script_transfer_nft = CP2PHK_script(colorId = colorId_nft, pubkey = self.pubkeys[0])

        txSuccess3 = CTransaction()
        txSuccess3.vin.append(CTxIn(COutPoint(coinbase_txs[1].malfixsha256, 0), b""))
        txSuccess3.vout.append(CTxOut(1, script_nft))
        sig_hash, err = SignatureHash(coinbase_txs[1].vout[0].scriptPubKey, txSuccess3, 0, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess3.vin[0].scriptSig = CScript([signature])
        txSuccess3.rehash()

        test_transaction_acceptance(node, txSuccess3, accepted=True)

        #TxSuccess4 - (UTXO-1)    - split REISSUABLE - 25 + 75     (UTXO-5,6)
        #           - (UTXO-3)    - split NON-REISSUABLE - 40 + 60 (UTXO-7,8)
        #           - coinbaseTx3 - issue 100 REISSUABLE           (UTXO-9)
        txSuccess4 = CTransaction()
        txSuccess4.vin.append(CTxIn(COutPoint(txSuccess1.malfixsha256, 0), b""))
        txSuccess4.vin.append(CTxIn(COutPoint(txSuccess2.malfixsha256, 0), b""))
        txSuccess4.vin.append(CTxIn(COutPoint(coinbase_txs[2].malfixsha256, 0), b""))
        txSuccess4.vout.append(CTxOut(25, script_reissuable))
        txSuccess4.vout.append(CTxOut(75, script_reissuable))
        txSuccess4.vout.append(CTxOut(40, script_nonreissuable))
        txSuccess4.vout.append(CTxOut(60, script_nonreissuable))
        txSuccess4.vout.append(CTxOut(100, script_reissuable))
        sig_hash, err = SignatureHash(txSuccess1.vout[0].scriptPubKey, txSuccess4, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess4.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess2.vout[0].scriptPubKey, txSuccess4, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess4.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[2].vout[0].scriptPubKey, txSuccess4, 2, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess4.vin[2].scriptSig = CScript([signature])
        txSuccess4.rehash()

        test_transaction_acceptance(node, txSuccess4, accepted=True)

        #txSuccess5 - (UTXO-6)    - split REISSUABLE(75)           (UTXO-10,11)
        #           - (UTXO-7)    - split NON-REISSUABLE(40)       (UTXO-12)
        #           - (UTXO-4)    - split NFT                      (UTXO-13)
        #           - coinbaseTx4
        txSuccess5 = CTransaction()
        txSuccess5.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 1), b""))
        txSuccess5.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 2), b""))
        txSuccess5.vin.append(CTxIn(COutPoint(txSuccess3.malfixsha256, 0), b""))
        txSuccess5.vin.append(CTxIn(COutPoint(coinbase_txs[3].malfixsha256, 0), b""))
        txSuccess5.vout.append(CTxOut(35, script_reissuable))
        txSuccess5.vout.append(CTxOut(40, script_reissuable))
        txSuccess5.vout.append(CTxOut(20, script_nonreissuable))
        txSuccess5.vout.append(CTxOut(20, script_nonreissuable))
        txSuccess5.vout.append(CTxOut(1, script_nft))
        txSuccess5.vout.append(CTxOut(1, script_nft)) #assumed to be NFT issue
        sig_hash, err = SignatureHash(txSuccess4.vout[1].scriptPubKey, txSuccess5, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess5.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess4.vout[2].scriptPubKey, txSuccess5, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess5.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess3.vout[0].scriptPubKey, txSuccess5, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess5.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[3].vout[0].scriptPubKey, txSuccess5, 3, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess5.vin[3].scriptSig = CScript([signature])
        txSuccess5.rehash()

        test_transaction_acceptance(node, txSuccess5, accepted=True)

        #TxSuccess6 - (UTXO-11)   - transfer REISSUABLE(40)        (UTXO-14)
        #           - (UTXO-8)    - burn NON-REISSUABLE(60)        (UTXO-15)*
        #           - (UTXO-13)   - transfer NFT                   (UTXO-16)
        #           - coinbaseTx5 - issue 1000 REISSUABLE1, change (UTXO-17)
        colorId_reissuable1 = colorIdReissuable(coinbase_txs[6].vout[0].scriptPubKey)
        script_reissuable1 = CP2PHK_script(colorId = colorId_reissuable, pubkey = self.pubkeys[0])

        txSuccess6 = CTransaction()
        txSuccess6.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 1), b""))
        txSuccess6.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 3), b""))
        txSuccess6.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 4), b""))
        txSuccess6.vin.append(CTxIn(COutPoint(coinbase_txs[4].malfixsha256, 0), b""))
        txSuccess6.vout.append(CTxOut(40, script_transfer_reissuable))
        txSuccess6.vout.append(CTxOut(30, script_transfer_nonreissuable))
        txSuccess6.vout.append(CTxOut(1, script_transfer_nft))
        txSuccess6.vout.append(CTxOut(1000, script_reissuable1))
        txSuccess6.vout.append(CTxOut(1*COIN, change_script))
        sig_hash, err = SignatureHash(txSuccess5.vout[1].scriptPubKey, txSuccess6, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess6.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess4.vout[3].scriptPubKey, txSuccess6, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess6.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess5.vout[4].scriptPubKey, txSuccess6, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess6.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[4].vout[0].scriptPubKey, txSuccess6, 3, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess6.vin[3].scriptSig = CScript([signature])
        txSuccess6.rehash()

        test_transaction_acceptance(node, txSuccess6, accepted=True)

        #txFailure1 - (UTXO-9,14) - aggregate REISSUABLE(40 + 100) x
        #           - (UTXO-12)   - burn NON-REISSUABLE(20)        (UTXO-19)*
        #           - (UTXO-4)    - transfer NFT                   x
        txFailure1 = CTransaction()
        txFailure1.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 4), b""))
        txFailure1.vin.append(CTxIn(COutPoint(txSuccess6.malfixsha256, 0), b""))
        txFailure1.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 2), b""))
        txFailure1.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 5), b""))
        txFailure1.vout.append(CTxOut(140, script_transfer_reissuable))
        txFailure1.vout.append(CTxOut(1, script_transfer_nft))
        sig_hash, err = SignatureHash(txSuccess4.vout[4].scriptPubKey, txFailure1, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txFailure1.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess6.vout[0].scriptPubKey, txFailure1, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txFailure1.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess4.vout[2].scriptPubKey, txFailure1, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txFailure1.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess5.vout[5].scriptPubKey, txFailure1, 3, SIGHASH_ALL)
        signature = self.privkeys[1].sign(sig_hash) + b'\x01'
        txFailure1.vin[3].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[1])])
        txFailure1.rehash()

        test_transaction_acceptance(node, txFailure1, accepted=False, reason=b'min relay fee not met')

        #TxFailure2 - (UTXO-17)   - convert REISSUABLE to NON-REISSUABLE
        txFailure2 = CTransaction()
        txFailure2.vin.append(CTxIn(COutPoint(txSuccess6.malfixsha256, 3), b""))
        txFailure2.vin.append(CTxIn(COutPoint(txSuccess6.malfixsha256, 4), b""))
        txFailure2.vout.append(CTxOut(60, script_transfer_nonreissuable))
        sig_hash, err = SignatureHash(txSuccess6.vout[3].scriptPubKey, txFailure2, 0, SIGHASH_ALL)
        signature = self.privkeys[1].sign(sig_hash) + b'\x01'
        txFailure2.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[1])])
        sig_hash, err = SignatureHash(txSuccess6.vout[4].scriptPubKey, txFailure2, 1, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txFailure2.vin[1].scriptSig = CScript([signature])
        txFailure2.rehash()

        test_transaction_acceptance(node, txFailure2, accepted=False, reason=b'invalid-colorid')


if __name__ == '__main__':
    ColoredCoinTest().main()
