#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test colored coin cupport in tapyrus wallet."""
from decimal import Decimal
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_array_result,
    assert_equal,
    assert_fee_amount,
    assert_raises_rpc_error,
    connect_nodes_bi,
    sync_blocks,
    sync_mempools,
    wait_until,
    hex_str_to_bytes,
    bytes_to_hex_str
)
from test_framework.messages import CTransaction, CTxIn, CTxOut, COutPoint, msg_tx, COIN, sha256, msg_block
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_1, OP_COLOR, hash160, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG, SignatureHash, SIGHASH_ALL
from test_framework.address import byte_to_base58

def colorIdReissuable(script):
    return b'\xc1' + sha256(script)

def CP2PHK_script(colorId, pubkey):
    pubkeyhash = hash160(hex_str_to_bytes(pubkey))
    return CScript([colorId, OP_COLOR, OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ])

class WalletColoredCoinTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True

        self.pubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc"]

        self.privkeys = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37","dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b"]
        
    def CP2SH_script(self, colorId):
        redeemScr = CScript([ hex_str_to_bytes(self.pubkeys[1]) ] + [OP_CHECKSIG])
        redeemScrhash = hash160(hex_str_to_bytes(redeemScr))
        return CScript([colorId, OP_COLOR, OP_HASH160, redeemScrhash, OP_EQUAL])

    def setup_network(self):
        self.add_nodes(4)
        self.start_node(0) 
        self.start_node(1) 
        self.start_node(2)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        self.sync_all([self.nodes[0:3]])

    def check_fee_amount(self, curr_balance, balance_with_fee, fee_per_byte, tx_size):
        """Return curr_balance after asserting the fee was in range"""
        fee = balance_with_fee - curr_balance
        assert_fee_amount(fee, tx_size, fee_per_byte * 1000)
        return curr_balance

    def get_vsize(self, txn):
        return self.nodes[0].decoderawtransaction(txn)['vsize']

    def run_test(self):
        # Check that there's no UTXO on none of the nodes
        assert_equal(len(self.nodes[0].listunspent()), 0)
        assert_equal(len(self.nodes[1].listunspent()), 0)
        assert_equal(len(self.nodes[2].listunspent()), 0)

        self.log.info("Mining blocks...")

        self.nodes[0].generate(1, self.signblockprivkey_wif)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 1)
        assert_equal(walletinfo['balance']['TPC'], 50)

        self.sync_all([self.nodes[0:3]])
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        assert_equal(self.nodes[0].getbalance(), 50)
        assert_equal(self.nodes[1].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 50)

        #  PART 1: using cp2pkh address
        # 
        utxo = self.nodes[0].listunspent()[0]
        new_address = self.nodes[0].getnewaddress()
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[0].getaddressinfo(new_address)["pubkey"]))
        colorid1 = b'\xc1' + sha256(hex_str_to_bytes(utxo['scriptPubKey']))
        cp2pkh_address = byte_to_base58(pubkeyhash, colorid1, 112)

        #  create colored transaction 1
        raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': utxo['txid'], 'vout': utxo['vout']}],
            outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2pkh_address : 100}],
        ), [], "ALL", self.options.scheme)['hex']
        txid_in_block = self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        self.sync_all([self.nodes[0:3]])
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        assert_equal(self.nodes[0].getbalance(), 39)
        assert_equal(self.nodes[1].getbalance(), 10)
        assert_equal(self.nodes[2].getbalance(), 101)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 2)
        assert_equal(walletinfo['balance']['TPC'], 39)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 100)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 2)
        assert_equal(walletinfo['balance']['TPC'], 10)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 0)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 1)
        assert_equal(walletinfo['balance']['TPC'], 101)

        #  PART 1: using cp2pkh address
        utxo = self.nodes[0].listunspent()[0]
        scripthash = hash160( CScript([OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ]) )
        colorid2 = b'\xc1' + sha256(hex_str_to_bytes(utxo['scriptPubKey']))
        cp2sh_address = byte_to_base58(scripthash, colorid2, 197)

         #  create colored transaction 2
        raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': utxo['txid'], 'vout': utxo['vout']}],
            outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 28}, { cp2sh_address : 100}],
        ), [], "ALL", self.options.scheme)['hex']
        txid_in_block = self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        self.sync_all([self.nodes[0:3]])
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(walletinfo['balance']['TPC'], 28)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 100)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 100)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(walletinfo['balance']['TPC'], 20)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 0)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 0)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 1)
        assert_equal(walletinfo['balance']['TPC'], 152)

        # send colored coins from node0 to node1 using sendtoaddress:
        '''new_address = self.nodes[1].getnewaddress()
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[1].getaddressinfo(new_address)["pubkey"]))
        cp2pkh_address = byte_to_base58(pubkeyhash, colorid1, 112)

        txid = self.nodes[0].sendtoaddress(cp2pkh_address, 10)
        self.sync_all([self.nodes[0:3]])
        print(self.nodes[0].listtransactions())
        print(self.nodes[1].listtransactions())
        print(self.nodes[2].listtransactions())

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid},
                            {"category": "send",
                            "token" : "TPC",
                            "amount": Decimal("-10.0"),
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid},
                            {"category": "receive",
                            "token" : "TPC",
                            "amount": Decimal("10.0"),
                            "confirmations": 0})
        # mine a block, confirmations should change:
        self.nodes[0].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])
        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid},
                            {"category": "send",
                            "token" : "TPC",
                            "amount": Decimal("-10.0"),
                            "confirmations": 1})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid},
                            {"category": "receive",
                            "token" : "TPC",
                            "amount": Decimal("10.0"),
                            "confirmations": 1})

        assert_equal(self.nodes[0].getbalance(), 39)
        assert_equal(self.nodes[1].getbalance(), 10)
        assert_equal(self.nodes[2].getbalance(), 101)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 2)
        assert_equal(walletinfo['balance']['TPC'], 39)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 100)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 100)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(walletinfo['balance']['TPC'], 20)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 0)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 0)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 1)
        assert_equal(walletinfo['balance']['TPC'], 152)'''

if __name__ == '__main__':
    WalletColoredCoinTest().main()
