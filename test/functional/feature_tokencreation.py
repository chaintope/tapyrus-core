#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2021 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test colored coin issue using create_colored_transaction python API in tapyrus wallet."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes, assert_raises_rpc_error, bytes_to_hex_str
from test_framework.blocktools import findTPC, create_colored_transaction
from test_framework.messages import sha256, CTransaction, COutPoint, CTxIn, CTxOut, COIN
from test_framework.script import CScript,OP_1, OP_EQUAL
from codecs import encode
from test_framework.blocktools import create_block

reverse_bytes = (lambda txid  : txid[-1: -len(txid)-1: -1])

BLOCK_1_COINBASE = "7258ed58b571f8bbff23c779f6d1e7f9c285700c4bcf0aa3f33292ff9983b5d7"
BLOCK_1_COINBASE_COLORID = "c2a54e3b8cdf649f1c80e16ba03a227b364565a4dabcfd3d0293b474200b456a42"

class TokenAPITest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.supports_cli = True
        self.setup_clean_chain = True

    def run_test(self):

        self.log.info("Creating blocks")
        tip = self.nodes[0].getbestblockhash()
        best_block = self.nodes[0].getblock(tip)
        block_time = best_block["time"] + 1
        #create a block with coinbase txid  this is the same across executions i.e with a constant script.
        coinbase = CTransaction()
        coinbase.vin.append(CTxIn(outpoint=COutPoint(0, 1), nSequence=0xffffffff))
        coinbaseoutput = CTxOut()
        coinbaseoutput.nValue = 50 * COIN
        coinbaseoutput.scriptPubKey = CScript([OP_1, OP_EQUAL])
        coinbase.vout = [coinbaseoutput]
        block1 = create_block(int(tip, 16), coinbase, block_time)
        block1.solve(self.signblockprivkey)
        self.nodes[0].submitblock(bytes_to_hex_str(block1.serialize()))

        #block1 is accepted and has a constant txid
        assert_equal(self.nodes[0].getbestblockhash(), block1.hash)
        assert_equal(BLOCK_1_COINBASE, block1.vtx[0].hashMalFix)

        #getcolor with the same txid should give the same colorid
        colorFromNode = self.nodes[0].getcolor(2, BLOCK_1_COINBASE, 0)
        assert_equal(colorFromNode, BLOCK_1_COINBASE_COLORID)
        self.sync_all()

        self.nodes[0].generate(2, self.signblockprivkey_wif)
        self.sync_all()
        self.nodes[1].generate(2, self.signblockprivkey_wif)
        self.sync_all()
        self.nodes[2].generate(2, self.signblockprivkey_wif)
        self.sync_all()

        self.log.info("Testing token issue using create_colored_transaction ")
        new_color1 = create_colored_transaction(1, 1000, self.nodes[0])['color']
        new_color2 = create_colored_transaction(2, 1000, self.nodes[1])['color']
        new_color3 = create_colored_transaction(3, 1, self.nodes[2])['color']

        self.sync_all()
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color1], 1000)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color2], 1000)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color3], 1)

        self.log.info("Testing token transfer using create_colored_transaction ")
        create_colored_transaction(1, 500, self.nodes[0], False, new_color1, self.nodes[1])
        create_colored_transaction(2, 500, self.nodes[1], False, new_color2, self.nodes[2])
        create_colored_transaction(3, 1, self.nodes[2], False, new_color3, self.nodes[0])

        self.sync_all()
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color1], 500)
        assert_equal(walletinfo['balance'][new_color3], 1)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color2], 500)
        assert_equal(walletinfo['balance'][new_color1], 500)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color3], 0)
        assert_equal(walletinfo['balance'][new_color2], 500)

        self.log.info("Testing getcolor")
        self.nodes[0].generate(3, self.signblockprivkey_wif)
        utxos = self.nodes[0].listunspent()

        # 1. REISSUABLE token
        tpc_spent = findTPC(utxos)
        utxo_ser = sha256(hex_str_to_bytes(tpc_spent['scriptPubKey']))
        colorid1 = 'c1' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(1, tpc_spent['scriptPubKey'])
        assert_equal(colorFromNode, colorid1)
        utxos.remove(tpc_spent)

        # 2. NON-REISSUABLE token 1 
        tpc_spent = findTPC(utxos)
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(tpc_spent['txid']))+ (tpc_spent['vout']).to_bytes(4, byteorder='little'))
        colorid2 = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(2, tpc_spent['txid'], tpc_spent['vout'])
        assert_equal(colorFromNode, colorid2)
        utxos.remove(tpc_spent)

        # 3. NFT token 1
        tpc_spent = findTPC(utxos)
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(tpc_spent['txid']))+ (tpc_spent['vout']).to_bytes(4, byteorder='little'))
        colorid3 = 'c3' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(3, tpc_spent['txid'], tpc_spent['vout'])
        assert_equal(colorFromNode, colorid3)
        utxos.remove(tpc_spent)

        # 4. NON-REISSUABLE token 2
        tpc_spent = findTPC(utxos)
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(tpc_spent['txid']))+ (tpc_spent['vout']).to_bytes(4, byteorder='little'))
        colorid4 = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(2, tpc_spent['txid'], tpc_spent['vout'])
        assert_equal(colorFromNode, colorid4)
        utxos.remove(tpc_spent)

        # 5. NFT token 2
        tpc_spent = findTPC(utxos)
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(tpc_spent['txid']))+ (tpc_spent['vout']).to_bytes(4, byteorder='little'))
        colorid5 = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(2, tpc_spent['txid'], tpc_spent['vout'])
        assert_equal(colorFromNode, colorid5)
        utxos.remove(tpc_spent)

        #negative tests
        unspent_tpc = findTPC(self.nodes[0].listunspent())

        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].getcolor, 4, unspent_tpc['txid'], unspent_tpc['vout'])
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].getcolor, 0, unspent_tpc['txid'], unspent_tpc['vout'])
        assert_raises_rpc_error(-8, "Extra parameter for Reissuable token", self.nodes[0].getcolor, 1, unspent_tpc['txid'], unspent_tpc['vout'])
        assert_raises_rpc_error(-8, "Parameter missing for Non-Reissuable or NFT token", self.nodes[0].getcolor, 2, unspent_tpc['txid'])
        assert_raises_rpc_error(-8, "Parameter missing for Non-Reissuable or NFT token", self.nodes[0].getcolor, 3, unspent_tpc['txid'])
        assert_raises_rpc_error(-8, "Invalid Tapyrus script", self.nodes[0].getcolor, 1, "ty")
        assert_raises_rpc_error(-8, "txid must be hexadecimal string (not 'ty')", self.nodes[0].getcolor, 3, "ty", 0)
        assert_raises_rpc_error(-8, "Invalid transaction id :0011001100110011001100110011001100110011001100110011001100110011", self.nodes[0].getcolor, 3, "0011001100110011001100110011001100110011001100110011001100110011", 0)


if __name__ == '__main__':
    TokenAPITest().main()