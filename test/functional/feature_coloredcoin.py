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

TxFailire1 - (UTXO-6)    - split REISSUABLE(75)           x
           - (UTXO-7)    - split NON-REISSUABLE(40)       x
           - (UTXO-4)    - split NFT                      x

TxSuccess5 - (UTXO-6)    - split REISSUABLE(75) - 30 + 45 (UTXO-10,11)
           - (UTXO-5)    - burn REISSUABLE(25)            (UTXO-12)*
           - (UTXO-8)    - transfer NON-REISSUABLE(60)    (UTXO-13)

TxSuccess6 - (UTXO-11)   - transfer REISSUABLE(45)        (UTXO-14)
           - (UTXO-13)   - burn NON-REISSUABLE(60)        (UTXO-15)*
           - (UTXO-4)    - transfer NFT                   (UTXO-16)
           - coinbaseTx4 - issue 1000 REISSUABLE1         (UTXO-17)

TxFailure2 - (UTXO-9,14) - aggregate REISSUABLE(45 + 100) (UTXO-18)x
           - (UTXO-7)    - burn NON-REISSUABLE(40)        x
           - (UTXO-4)    - transfer NFT                   x
           - (UTXO-18)   - burn REISSUABLE(45)            x

TxSuccess7 - (UTXO-9,14) - aggregate REISSUABLE(45 + 100) (UTXO-18)
           - (UTXO-7)    - burn NON-REISSUABLE(40)        (UTXO-19)*
           - (UTXO-4)    - transfer NFT                   (UTXO-20)

TxFailire3 - (UTXO-15)   - split  NON-REISSUABLE(60)      x

TxFailire4 - coinbaseTx4 - issue 1000 REISSUABLE2         x

TxSuccess8 - (UTXO-18)   - split REISSUABLE(145) into 10  (UTXO-21-30)
           - coinbaseTx5 - issue 1000 REISSUABLE1         (UTXO-31)

TxFailire5 - (UTXO-21,31)- aggregate REISSUABLE & REISSUABLE1 x

TxFailire6 - (UTXO-17)   - convert REISSUABLE1 to NON-REISSUABLE1 x

TxSuccess9 - (UTXO-17,31)- aggregate REISSUABLE1           (UTXO-32)
           - (UTXO-21,31)- aggregate REISSUABLE            (UTXO-33)
           - coinbaseTx6 - issue 100 NON-REISSUABLE1       (UTXO-34)
'''

import shutil, os
import time

from io import BytesIO

from test_framework.blocktools import create_block, create_coinbase, create_tx_with_script, create_transaction
from test_framework.messages import CTransaction, CTxIn, CTxOut, COutPoint, msg_tx
from test_framework.key import CECKey
from test_framework.schnorr import Schnorr
from test_framework.mininode import P2PDataStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes, bytes_to_hex_str, assert_raises_rpc_error, NetworkDirName, hash256
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_1, OP_COLOR, hash160, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG, SignatureHash, SIGHASH_ALL

def colorIdReissuable(script):
    return hash256(b'01'+ script)

def colorIdNonReissuable(script):
    return (b'02'+ script)

def colorIdNFT(script):
    return (b'03'+ script)

def CP2PHK_script(colorId, pubkey):
    pubkeyhash = hash160(hex_str_to_bytes(pubkey))
    return CScript([colorId, OP_COLOR, OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ])

def CP2SH_script(colorId, redeemScr):
    redeemScrhash = hash160(hex_str_to_bytes(redeemScr))
    return CScript([colorId, OP_COLOR, OP_HASH160, redeemScrhash, OP_EQUAL])

def test_transaction_acceptance(node, tx, accepted, reason=None):
    """Send a transaction to the node and check that it's accepted to the mempool"""
    tx_message = msg_tx(tx)
    node.p2p.send_message(tx_message)
    node.p2p.sync_with_ping()
    assert_equal(tx.hashMalFix in node.getrawmempool(), accepted)
    if (reason is not None and not accepted):
        # Check the rejection reason as well.
        with mininode_lock:
            assert_equal(node.last_message["reject"].reason, reason)

class ColoredCoinTest(BitcoinTestFramework):
    def set_test_params(self):
        self.pubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc"]
        
        self.privkey = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37",
        "dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b"]

        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))

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
        blocks = node.generate(10, self.signblockprivkey)
        coinbase_txs = []
        for i in blocks:
            txid = node.getblock(i)['tx'][0]
            coinbase_tx = CTransaction()
            coinbase_tx.deserialize(BytesIO(hex_str_to_bytes( node.gettransaction(txid)['hex'])))
            coinbase_tx.rehash()
            coinbase_txs.append(coinbase_tx)

        colorId = colorIdReissuable(coinbase_txs[0].vout[0].scriptPubKey)
        script = CP2PHK_script(colorId = colorId, pubkey = self.pubkeys[0])

        txSuccess1 = CTransaction()
        txSuccess1.vin.append(CTxIn(COutPoint(coinbase_txs[0].malfixsha256, 0), b""))
        txSuccess1.vout.append(CTxOut(coinbase_txs[0].vout[0].nValue, script))
        sig_hash, err = SignatureHash(script, txSuccess1, 0, SIGHASH_ALL)
        txSuccess1.vin[0].scriptSig = self.coinbase_key.sign(sig_hash) + b'\x01'  # 0x1 is SIGHASH_ALL
        txSuccess1.rehash()

        test_transaction_acceptance(node, txSuccess1, accepted=False)

if __name__ == '__main__':
    ColoredCoinTest().main()
