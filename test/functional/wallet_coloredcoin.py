#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test colored coin support in tapyrus wallet.
    Wallet is able to :
        1. identify CP2SH and CP2PKH scripts
        2. maintain token balance
        3. create transaction to send colored coins
    
    RPCs:
        1. sendtoaddress
        2. issuetoken
        3. transfertoken
        4. burntoken
        5. getcolor

    """
from decimal import Decimal
from codecs import encode
import struct

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_array_result,
    assert_equal,
    assert_fee_amount,
    connect_nodes_bi,
    hex_str_to_bytes,
    bytes_to_hex_str
)
from test_framework.messages import sha256
from test_framework.script import CScript, hash160, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.address import byte_to_base58

class WalletColoredCoinTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True

        self.pubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc"]

        self.privkeys = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37","dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b"]

        self.colorids = []

        # balance_expected_<node> arrays are populated like this:
        # #  RPC   # balance [ TPC, colorid1, 2, 3, 4, 5, 6] 
        # [create            ] 
        # [sendtoaddress     ]
        # [transfertoken     ]
        # [burntoken         ]
        # [issuetoken        ]

        self.balance_expected_node0 = []
        self.balance_expected_node1 = []
        self.balance_expected_node2 = [] 

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

        assert_array_result(self.nodes[0].listunspent(),
                            {"txid": txid_in_block,
                            "token" : bytes_to_hex_str(colorid1)},
                            {"amount": 100,
                            "confirmations": 1})

        colorFromNode = self.nodes[0].getcolor(1, utxo['scriptPubKey'])
        assert_equal(hex_str_to_bytes(colorFromNode), colorid1)
        colorFromNode = self.nodes[0].getcolor(2, txid_in_block, 1)
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(txid_in_block))+ (1).to_bytes(4, byteorder='little'))
        coloridexpected = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        assert_equal(colorFromNode, coloridexpected)

        #  PART 2: using cp2sh address
        utxo = self.nodes[0].listunspent()[0]
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[1].getaddressinfo(self.nodes[1].getnewaddress())["pubkey"]))
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
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 0)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(walletinfo['balance']['TPC'], 20)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 0)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 100)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 1)
        assert_equal(walletinfo['balance']['TPC'], 152)

        assert_array_result(self.nodes[1].listunspent(),
                            {"txid": txid_in_block,
                            "token" : bytes_to_hex_str(colorid2)},
                            {"amount": 100,
                            "confirmations": 1})

        colorFromNode = self.nodes[0].getcolor(1, utxo['scriptPubKey'])
        assert_equal(hex_str_to_bytes(colorFromNode), colorid2)
        colorFromNode = self.nodes[0].getcolor(2, txid_in_block, 1)
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(txid_in_block))+ (1).to_bytes(4, byteorder='little'))
        coloridexpected = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        assert_equal(colorFromNode, coloridexpected)

        # send colored coins  (colorid1) from node0 to node1 using sendtoaddress:
        new_address = self.nodes[1].getnewaddress()
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[1].getaddressinfo(new_address)["pubkey"]))
        cp2pkh_address = byte_to_base58(pubkeyhash, colorid1, 112)

        self.log.debug("Testing sendtoaddress")
        txid = self.nodes[0].sendtoaddress(cp2pkh_address, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid},
                            {"category": "send",
                            "token" : bytes_to_hex_str(colorid1),
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid},
                            {"category": "receive",
                            "token" : bytes_to_hex_str(colorid1),
                            "amount": 10,
                            "confirmations": 0})
        #mine a block, confirmations should change:
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])
        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid},
                            {"category": "send",
                            "token" : bytes_to_hex_str(colorid1),
                            "amount": -10,
                            "confirmations": 1})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid},
                            {"category": "receive",
                            "token" : bytes_to_hex_str(colorid1),
                            "amount": 10,
                            "confirmations": 1})


        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '27.99990480')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 90)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 0)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(walletinfo['balance']['TPC'], 20)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 10)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 100)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 1)
        assert_equal(str(walletinfo['balance']['TPC']), '202.00009520')

        # send colored coins (colorid2) from node1 to node0 using transfertoken:
        new_address = self.nodes[0].getnewaddress()
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[0].getaddressinfo(new_address)["pubkey"]))
        cp2pkh_address = byte_to_base58(pubkeyhash, colorid2, 112)

        self.log.debug("Testing transfertoken")
        txid_transfer = self.nodes[1].transfertoken(cp2pkh_address, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid_transfer},
                            {"category": "send",
                            "token" : bytes_to_hex_str(colorid2),
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid_transfer},
                            {"category": "receive",
                            "token" : bytes_to_hex_str(colorid2),
                            "amount": 10,
                            "confirmations": 0})
        #mine a block, confirmations should change:
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid_transfer},
                            {"category": "send",
                            "token" : bytes_to_hex_str(colorid2),
                            "amount": -10,
                            "confirmations": 1})
        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid_transfer},
                            {"category": "receive",
                            "token" : bytes_to_hex_str(colorid2),
                            "amount": 10,
                            "confirmations": 1})


        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '27.99990480')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 90)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 10)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '19.99992080')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 10)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 90)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 1)
        assert_equal(str(walletinfo['balance']['TPC']), '252.00017440')

        self.log.debug("Testing getcolor")
        node2_utxos = self.nodes[2].listunspent()
        colorFromNode = self.nodes[2].getcolor(2, node2_utxos[0]['txid'], node2_utxos[0]['vout'])
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(node2_utxos[0]['txid']))+ (node2_utxos[0]['vout']).to_bytes(4, byteorder='little'))
        colorid3 = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        assert_equal(colorFromNode, colorid3)

        colorFromNode = self.nodes[2].getcolor(3, node2_utxos[1]['txid'], node2_utxos[1]['vout'])
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(node2_utxos[1]['txid']))+ (node2_utxos[0]['vout']).to_bytes(4, byteorder='little'))
        colorid4 = 'c3' + encode(utxo_ser, 'hex_codec').decode('ascii')
        assert_equal(colorFromNode, colorid4)


        self.log.debug("Testing issuetoken")
        res = self.nodes[2].issuetoken(2, 100, node2_utxos[0]['txid'], node2_utxos[0]['vout'])
        txid_issue = res['txid']
        assert_equal(res['color'], colorid3)

        assert_array_result(self.nodes[2].listtransactions(),
                            {"txid": txid_issue},
                            {"category": "receive",
                            "token" : colorid3,
                            "amount": 100,
                            "confirmations": 0})
        #mine a block, confirmations should change:
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])
        assert_array_result(self.nodes[2].listtransactions(),
                            {"txid": txid_issue},
                            {"category": "receive",
                            "token" : colorid3,
                            "amount": 100,
                            "confirmations": 1})

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '27.99990480')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 90)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 10)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '19.99992080')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 10)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 90)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 2)
        assert_equal(str(walletinfo['balance']['TPC']), '302.00017440')
        assert_equal(walletinfo['balance'][colorid3], 100)

        self.log.debug("Testing burntoken")
        txid_burn = self.nodes[1].burntoken(bytes_to_hex_str(colorid2), 20)

        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '27.99990480')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 90)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 10)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '19.99983940')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 10)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 70)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 2)
        assert_equal(str(walletinfo['balance']['TPC']), '352.00017440')
        assert_equal(walletinfo['balance'][colorid3], 100)

        txid_burn = self.nodes[0].burntoken(bytes_to_hex_str(colorid1), 90)

        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '27.99983720')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 0)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 10)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 3)
        assert_equal(str(walletinfo['balance']['TPC']), '19.99983940')
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid1)], 10)
        assert_equal(walletinfo['balance'][bytes_to_hex_str(colorid2)], 70)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(len(walletinfo['balance']), 2)
        assert_equal(str(walletinfo['balance']['TPC']), '402.00025580')
        assert_equal(walletinfo['balance'][colorid3], 100)

reverse_bytes = (lambda txid  : txid[-1: -len(txid)-1: -1])

if __name__ == '__main__':
    WalletColoredCoinTest().main()
