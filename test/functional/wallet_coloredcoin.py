#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test colored coin support in tapyrus wallet.
    Wallet is able to :
        1. identify CP2SH and CP2PKH scripts
        2. maintain token balance
        3. create transaction to issue/send/burn colored coins
    
    RPCs:
        1. sendtoaddress
        2. issuetoken
        3. transfertoken
        4. burntoken
        5. getcolor
        6. reissuetoken
        7. createrawtransaction
        8. sendtoaddress


    """
from codecs import encode
import math
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_array_result,
    assert_equal,
    connect_nodes_bi,
    connect_nodes,
    disconnect_nodes,
    hex_str_to_bytes,
    bytes_to_hex_str,
    assert_raises_rpc_error,
    sync_blocks
)
from test_framework.messages import sha256
from test_framework.script import CScript, hash160, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.address import byte_to_base58
from test_framework.blocktools import findTPC

class WalletColoredCoinTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.supports_cli = True
        self.setup_clean_chain = True
        self.extra_args = [["-dustrelayfee=0"], ["-dustrelayfee=0"], ["-dustrelayfee=0"], ["-dustrelayfee=0"]]

        self.pubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc"]

        self.privkeys = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37","dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b"]

        # this is the complete list of all colorids generated in this test.
        # this helps in balance verification using the below list
        self.colorids = []

        # this is the complete balance info of nodes 0, 1 and 2
        # stage represents the stage in the test case when balance is verified
        # first member of each stage is the total number of tokens in the wallet (including TPC)
        self.balance_expected = [
                     [ #'''nodes 0'''
                        [ 1, 300],                                #stage 0
                        [ 4, 267, 100, 100, 1],                   #stage 1 create part1
                        [ 6, 234, 200, 100, 1, 100, 1],           #stage 2 create part2
                        [ 6, 233, 180, 90, 0, 90, 0],             #stage 3 sendtoaddress
                        [ 6, 233, 160, 80, 0, 80, 0],             #stage 4 transfertoken
                        [ 6, 233, 140, 60, 0, 60, 0],             #stage 5 burn partial
                        [ 6, 233, 140, 60, 0, 60, 0],             #stage 6 burn full
                        [ 7, 283, 140, 60, 0, 60, 0, 200],        #stage 7 reissue
                      ], 
                     [ #'''nodes 1'''
                        [ 0, 0],                     #stage 0
                        [ 4, 30, 0, 0, 0],           #stage 1
                        [ 6, 60, 0, 0, 0, 0, 0],     #stage 2
                        [ 6, 60, 20, 10, 1, 10, 1],  #stage 3
                        [ 6, 60, 40, 20, 1, 20, 1],  #stage 4
                        [ 6, 60, 40, 20, 1, 20, 1],  #stage 5
                        [ 4, 59, 0, 0, 0],           #stage 6
                        [ 4, 59, 0, 0, 0],           #stage 7
                      ],
                     [ #'''nodes 2'''
                        [ 1, 50],                 #stage 0
                        [ 1, 103],                #stage 1
                        [ 1, 156],                #stage 2
                        [ 1, 206],                #stage 3
                        [ 1, 256],                #stage 4
                        [ 1, 306],                #stage 5
                        [ 1, 356],                #stage 6
                        [ 1, 406],                #stage 7
                      ] 
        ]

    #class variable to remember the stage of testing
    stage = 0

    def test_nodeBalances(self):
        ''' check all node balances in one place:'''
        #context = decimal.getcontext()
        #context.prec = 6

        for i in [0, 1, 2]: #node
            walletinfo = self.nodes[i].getwalletinfo()
            assert_equal(len(walletinfo['balance']), self.balance_expected[i][self.stage][0])
            assert_equal(math.floor(self.nodes[i].getbalance()), self.balance_expected[i][self.stage][1])
            for j in range(2, self.balance_expected[i][self.stage][0]): #colorid
                assert_equal(walletinfo['balance'][self.colorids[j-1]], self.balance_expected[i][self.stage][j])
        self.stage += 1

    def setup_network(self):
        self.add_nodes(4)
        self.start_node(0) 
        self.start_node(1) 
        self.start_node(2)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        self.sync_all([self.nodes[0:3]])

    def test_getcolorRPC(self, utxos):
        '''test getcolor RPC using 2 different types of color addresses '''

        self.log.info("Testing getcolor")
        # 1. REISSUABLE token
        utxo_ser = sha256(hex_str_to_bytes(utxos[0]['scriptPubKey']))
        colorid1 = 'c1' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(1, utxos[0]['scriptPubKey'])
        assert_equal(colorFromNode, colorid1)

        # 2. NON-REISSUABLE token 1 
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(utxos[1]['txid']))+ (utxos[1]['vout']).to_bytes(4, byteorder='little'))
        colorid2 = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(2, utxos[1]['txid'], utxos[1]['vout'])
        assert_equal(colorFromNode, colorid2)

        # 3. NFT token 1
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(utxos[2]['txid']))+ (utxos[2]['vout']).to_bytes(4, byteorder='little'))
        colorid3 = 'c3' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(3, utxos[2]['txid'], utxos[2]['vout'])
        assert_equal(colorFromNode, colorid3)

        # 4. NON-REISSUABLE token 2
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(utxos[3]['txid']))+ (utxos[3]['vout']).to_bytes(4, byteorder='little'))
        colorid4 = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(2, utxos[3]['txid'], utxos[3]['vout'])
        assert_equal(colorFromNode, colorid4)

        # 5. NFT token 2
        utxo_ser = sha256(reverse_bytes(hex_str_to_bytes(utxos[4]['txid']))+ (utxos[4]['vout']).to_bytes(4, byteorder='little'))
        colorid5 = 'c2' + encode(utxo_ser, 'hex_codec').decode('ascii')
        colorFromNode = self.nodes[0].getcolor(2, utxos[4]['txid'], utxos[4]['vout'])
        assert_equal(colorFromNode, colorid5)

        self.colorids = ['00', colorid1, colorid2, colorid3, colorid4, colorid5]

        #negative tests
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].getcolor, 4, utxos[4]['txid'], utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].getcolor, 0, utxos[4]['txid'], utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Extra parameter for Reissuable token", self.nodes[0].getcolor, 1, utxos[4]['txid'], utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Parameter missing for Non-Reissuable or NFT token", self.nodes[0].getcolor, 2, utxos[4]['txid'])
        assert_raises_rpc_error(-8, "Parameter missing for Non-Reissuable or NFT token", self.nodes[0].getcolor, 3, utxos[4]['txid'])
        assert_raises_rpc_error(-8, "Invalid Tapyrus script", self.nodes[0].getcolor, 1, "ty")
        assert_raises_rpc_error(-8, "txid must be hexadecimal string (not 'ty')", self.nodes[0].getcolor, 3, "ty", 0)
        assert_raises_rpc_error(-8, "Invalid transaction id :0011001100110011001100110011001100110011001100110011001100110011", self.nodes[0].getcolor, 3, "0011001100110011001100110011001100110011001100110011001100110011", 0)

    def test_createrawtransaction(self, utxos):
        '''test transaction with colored output using createrawtransaction 
        all 3 types of tokens and addresses should be successful'''

        self.log.info("Testing createrawtransaction")
        #pubkeyhash and script hash for colored addresses
        new_address = self.nodes[0].getnewaddress()
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[0].getaddressinfo(new_address)["pubkey"]))
        scripthash = hash160( CScript([OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ]) )

        cp2pkh_address1 = byte_to_base58(pubkeyhash, hex_str_to_bytes(self.colorids[1]), 112)
        cp2sh_address1 = byte_to_base58(scripthash, hex_str_to_bytes(self.colorids[1]), 197)

        cp2pkh_address2 = byte_to_base58(pubkeyhash, hex_str_to_bytes(self.colorids[2]), 112)
        cp2pkh_address3 = byte_to_base58(pubkeyhash, hex_str_to_bytes(self.colorids[3]), 112)

        cp2sh_address4 = byte_to_base58(scripthash, hex_str_to_bytes(self.colorids[4]), 197)
        cp2sh_address5 = byte_to_base58(scripthash, hex_str_to_bytes(self.colorids[5]), 197)


        #  PART 1: using cp2pkh address
        # 
        #  create transaction 1 with colorid1
        if self.options.usecli:
            rawtx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d }]" % (utxos[5]['txid'], utxos[5]['vout']),
                "[{\"%s\": 10}, {\"%s\" : 39}, { \"%s\" : 100}]" % (self.nodes[1].getnewaddress(), self.nodes[0].getnewaddress(), cp2pkh_address1))
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet("%s"%rawtx, [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction("%s"%raw_tx_in_block, True)
        else:
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
                inputs=[{'txid': utxos[5]['txid'], 'vout': utxos[5]['vout']}],
                outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2pkh_address1 : 100}],
            ), [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        #  create transaction 2 with colorid2
        if self.options.usecli:
            rawtx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d }]" % (utxos[1]['txid'], utxos[1]['vout']),
                "[{\"%s\": 10}, {\"%s\" : 39}, { \"%s\" : 100}]" % (self.nodes[1].getnewaddress(), self.nodes[0].getnewaddress(), cp2pkh_address2))
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet("%s"%rawtx, [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction("%s"%raw_tx_in_block, True)
        else:
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
                inputs=[{'txid': utxos[1]['txid'], 'vout': utxos[1]['vout']}],
                outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2pkh_address2 : 100}],
            ), [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        #  create transaction 3 with colorid3
        if self.options.usecli:
            rawtx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d }]" % (utxos[2]['txid'], utxos[2]['vout']),
                "[{\"%s\": 10}, {\"%s\" : 39}, { \"%s\" : 1}]" % (self.nodes[1].getnewaddress(), self.nodes[0].getnewaddress(), cp2pkh_address3))
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet("%s"%rawtx, [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction("%s"%raw_tx_in_block, True)
        else:
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
                inputs=[{'txid': utxos[2]['txid'], 'vout': utxos[2]['vout']}],
                outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2pkh_address3 : 1}],
            ), [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        self.sync_all([self.nodes[0:3]])
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        self.test_nodeBalances()

        #  PART 2: using cp2sh address
        # 
        #  create transaction 1 with colorid1
        if self.options.usecli:
            rawtx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d }]" % (utxos[0]['txid'], utxos[0]['vout']),
                "[{\"%s\": 10}, {\"%s\" : 39}, { \"%s\" : 100}]" % (self.nodes[1].getnewaddress(), self.nodes[0].getnewaddress(), cp2sh_address1))
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet("%s"%rawtx, [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction("%s"%raw_tx_in_block, True)
        else:
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
                inputs=[{'txid': utxos[0]['txid'], 'vout': utxos[0]['vout']}],
                outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2sh_address1 : 100}],
            ), [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        #  create transaction 2 with colorid4
        if self.options.usecli:
            rawtx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d }]" % (utxos[3]['txid'], utxos[3]['vout']),
                "[{\"%s\": 10}, {\"%s\" : 39}, { \"%s\" : 100}]" % (self.nodes[1].getnewaddress(), self.nodes[0].getnewaddress(), cp2sh_address4))
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet("%s"%rawtx, [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction("%s"%raw_tx_in_block, True)
        else:
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': utxos[3]['txid'], 'vout': utxos[3]['vout']}],
            outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2sh_address4 : 100}],
        ), [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        #  create transaction 3 with colorid5
        if self.options.usecli:
            rawtx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d }]" % (utxos[4]['txid'], utxos[4]['vout']),
                "[{\"%s\": 10}, {\"%s\" : 39}, { \"%s\" : 1}]" % (self.nodes[1].getnewaddress(), self.nodes[0].getnewaddress(), cp2sh_address5))
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet("%s"%rawtx, [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction("%s"%raw_tx_in_block, True)
        else:
            raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': utxos[4]['txid'], 'vout': utxos[4]['vout']}],
            outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2sh_address5 : 1}],
        ), [], "ALL", self.options.scheme)['hex']
            self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        self.sync_all([self.nodes[0:3]])
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        self.test_nodeBalances()

    def test_sendRPC(self, rpcname):
        self.log.info("Testing %s" % rpcname)

        #pubkeyhash and script hash for colored addresses on node 1
        new_address = self.nodes[1].getnewaddress()
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[1].getaddressinfo(new_address)["pubkey"]))
        scripthash = hash160( CScript([OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ]) )

        cp2pkh_address1 = byte_to_base58(pubkeyhash, hex_str_to_bytes(self.colorids[1]), 112)
        cp2sh_address1 = byte_to_base58(scripthash, hex_str_to_bytes(self.colorids[1]), 197)

        cp2pkh_address2 = byte_to_base58(pubkeyhash, hex_str_to_bytes(self.colorids[2]), 112)
        cp2pkh_address3 = byte_to_base58(pubkeyhash, hex_str_to_bytes(self.colorids[3]), 112)

        cp2sh_address4 = byte_to_base58(scripthash, hex_str_to_bytes(self.colorids[4]), 197)
        cp2sh_address5 = byte_to_base58(scripthash, hex_str_to_bytes(self.colorids[5]), 197)

        sendrpc = getattr(self.nodes[0], rpcname, lambda x: print("no method %s" % x))
        txid1 = sendrpc(cp2pkh_address1, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid1, "category": "send"},
                            {"token" : self.colorids[1],
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid1, "category": "receive"},
                            {"token" : self.colorids[1],
                            "amount": 10,
                            "confirmations": 0})

        txid2 = sendrpc(cp2sh_address1, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid2, "category": "send"},
                            {"token" : self.colorids[1],
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid2, "category": "receive"},
                            {"token" : self.colorids[1],
                            "amount": 10,
                            "confirmations": 0})

        txid3 = sendrpc(cp2pkh_address2, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid3, "category": "send"},
                            {"token" : self.colorids[2],
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid3, "category": "receive"},
                            {"token" : self.colorids[2],
                            "amount": 10,
                            "confirmations": 0})

        #NFT can be transferred only once. so call it only the first time
        if(rpcname == "sendtoaddress"):
            txid4 = sendrpc(cp2pkh_address3, 1)
            self.sync_all([self.nodes[0:3]])

            assert_array_result(self.nodes[0].listtransactions(),
                                {"txid": txid4, "category": "send"},
                                {"token" : self.colorids[3],
                                "amount": -1,
                                "confirmations": 0})
            assert_array_result(self.nodes[1].listtransactions(),
                                {"txid": txid4, "category": "receive"},
                                {"token" : self.colorids[3],
                                "amount": 1,
                                "confirmations": 0})

        txid5 = sendrpc(cp2sh_address4, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid5, "category": "send"},
                            {"token" : self.colorids[4],
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid5, "category": "receive"},
                            {"token" : self.colorids[4],
                            "amount": 10,
                            "confirmations": 0})

        #NFT can be transferred only once. so call it only the first time
        if(rpcname == "sendtoaddress"):
            txid6 = sendrpc(cp2sh_address5, 1)
            self.sync_all([self.nodes[0:3]])

            assert_array_result(self.nodes[0].listtransactions(),
                                {"txid": txid6, "category": "send"},
                                {"token" : self.colorids[5],
                                "amount": -1,
                                "confirmations": 0})
            assert_array_result(self.nodes[1].listtransactions(),
                                {"txid": txid6, "category": "receive"},
                                {"token" : self.colorids[5],
                                "amount": 1,
                                "confirmations": 0})
        # Verify that multiple entries exist for the same txid after sending a token
        txlist = self.nodes[0].listtransactions(count=50)
        entries = [e for e in txlist if e['txid'] == txid1]
        assert len(entries) >= 2, "Token tx should have multiple entries (send + fee)"

        # Verify the existence of a fee entry with a negative amount
        fee_entries = [e for e in txlist if e['txid'] == txid1 and e['category'] == 'fee']
        assert len(fee_entries) >= 1, "Expected a fee entry for txid1"
        assert fee_entries[0]['amount'] < 0, "Fee amount should be negative"

        #mine a block, confirmations should change:
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])
        unspent = self.nodes[0].listunspent()
        assert_array_result(unspent, {"txid": txid1, "token" : self.colorids[1]},
                            {"amount": self.balance_expected[0][self.stage][3], "confirmations": 1})
        assert_array_result(unspent, {"txid": txid2, "token" : self.colorids[1]},
                            {"amount": self.balance_expected[0][self.stage][3], "confirmations": 1})
        assert_array_result(unspent, {"txid": txid3, "token" : self.colorids[2]},
                            {"amount": self.balance_expected[0][self.stage][5], "confirmations": 1})
        assert_array_result(unspent, {"txid": txid5, "token" : self.colorids[4]},
                            {"amount": self.balance_expected[0][self.stage][5], "confirmations": 1})

        txlist = self.nodes[0].listtransactions(count=50)
        assert_array_result(txlist,{"txid": txid1},{"confirmations": 1})
        assert_array_result(txlist,{"txid": txid2},{"confirmations": 1})
        assert_array_result(txlist,{"txid": txid3},{"confirmations": 1})
        assert_array_result(txlist,{"txid": txid5},{"confirmations": 1})

        if(rpcname == "sendtoaddress"):
            assert_array_result(txlist,{"txid": txid4},{"confirmations": 1})
            assert_array_result(txlist,{"txid": txid6},{"confirmations": 1})
            unspent = self.nodes[1].listunspent()
            assert_array_result(unspent, {"txid": txid4, "token" : self.colorids[3]},
                                {"amount": 1, "confirmations": 1})
            assert_array_result(unspent, {"txid": txid6, "token" : self.colorids[5]},
                                {"amount": 1, "confirmations": 1})
        self.test_nodeBalances()

        #negative
        if not self.options.usecli:
            assert_raises_rpc_error(-3, "Invalid amount", sendrpc, cp2pkh_address1, 'foo')
            assert_raises_rpc_error(-3, "Invalid amount", sendrpc, cp2pkh_address1, '66ae')
            assert_raises_rpc_error(-3, "Invalid amount", sendrpc, cp2pkh_address1, 66.99)

    def test_burntoken(self):
        self.log.info("Testing burntoken")
        #partial
        txid1 = self.nodes[0].burntoken(self.colorids[1], 20)
        txid2 = self.nodes[0].burntoken(self.colorids[2], 20)
        self.nodes[0].burntoken(self.colorids[4], 20)

        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        self.test_nodeBalances()

        #check if there is a warning when signing burn tokens
        if self.options.usecli:
            address = self.nodes[1].getnewaddress("", "%s" % self.colorids[1])
            tx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d },{\"txid\": \"%s\", \"vout\": %d }]" % (txid1, 0, txid1, 1),
                "[{\"%s\": 10}]" % (address))
            burn_warning = self.nodes[0].signrawtransactionwithwallet("%s"%tx, [], "ALL", self.options.scheme)['warning']
        else:
            burn_warning = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': txid1, 'vout': 0}, {'txid': txid1, 'vout': 1}],
            outputs=[{self.nodes[1].getnewaddress("", self.colorids[1]): 10}]), [], "ALL", self.options.scheme)['warning']
        assert_equal(burn_warning, "token burn detected")

        if self.options.usecli:
            address = self.nodes[1].getnewaddress("", "%s" %self.colorids[1])
            tx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d },{\"txid\": \"%s\", \"vout\": %d }]" % (txid1, 0, txid1, 1),
                "[{\"%s\": 20}]" % (address))
            burn_warning = self.nodes[0].signrawtransactionwithwallet("%s"%tx, [], "ALL", self.options.scheme)['warning']
        else:
            burn_warning = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': txid1, 'vout': 0}, {'txid': txid1, 'vout': 1}],
            outputs=[{self.nodes[1].getnewaddress("", self.colorids[1]): 20}]), [], "ALL", self.options.scheme)['warning']
        assert_equal(burn_warning, "token burn detected")

        if self.options.usecli:
            address = self.nodes[1].getnewaddress("", "%s" % (self.colorids[1]))
            tx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d },{\"txid\": \"%s\", \"vout\": %d }]" % (txid1, 0, txid1, 1),
                "[{\"%s\": 60}]" % (address))
            burn_warning = self.nodes[0].signrawtransactionwithwallet("%s"%tx, [], "ALL", self.options.scheme)
        else:
            burn_warning = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': txid1, 'vout': 0}, {'txid': txid1, 'vout': 1}],
            outputs=[{self.nodes[1].getnewaddress("", self.colorids[1]): 60}]), [], "ALL", self.options.scheme)
        assert('warning' not in burn_warning.keys())

        if self.options.usecli:
            address1 = self.nodes[1].getnewaddress("", "%s" % (self.colorids[2]))
            address2 = self.nodes[0].getnewaddress("", "%s" % (self.colorids[2]))
            tx = self.nodes[0].createrawtransaction(
                "[{\"txid\": \"%s\", \"vout\": %d },{\"txid\": \"%s\", \"vout\": %d }]" % (txid2, 0, txid2, 1),
                "[{\"%s\": 40}, {\"%s\": 20}]" % (address1, address2))
            burn_warning = self.nodes[0].signrawtransactionwithwallet("%s"%tx, [], "ALL", self.options.scheme)
        else:
            burn_warning = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': txid2, 'vout': 0}, {'txid': txid2, 'vout': 1}],
            outputs=[{self.nodes[1].getnewaddress("", self.colorids[2]): 40}, {self.nodes[0].getnewaddress("", self.colorids[2]): 20}]), [], "ALL", self.options.scheme)
        assert('warning' not in burn_warning.keys())

        #full burn
        self.nodes[1].burntoken(self.colorids[1], 40)
        self.nodes[1].burntoken(self.colorids[2], 20)
        self.nodes[1].burntoken(self.colorids[3], 1)
        self.nodes[1].burntoken(self.colorids[4], 20)
        self.nodes[1].burntoken(self.colorids[5], 1)

        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        self.test_nodeBalances()

        #negative cases
        if not self.options.usecli:
            assert_raises_rpc_error(-3, "Invalid amount", self.nodes[0].burntoken, self.colorids[1], 'foo')
            assert_raises_rpc_error(-3, "Invalid amount", self.nodes[0].burntoken, self.colorids[1], '66ae')
            assert_raises_rpc_error(-3, "Invalid amount", self.nodes[0].burntoken, self.colorids[1], 66.99)
            assert_raises_rpc_error(-1, "Burn colored coins or tokens in the wallet", self.nodes[0].burntoken, "00")
            assert_raises_rpc_error(-1, "Burn colored coins or tokens in the wallet", self.nodes[0].burntoken, "c4")
            assert_raises_rpc_error(-1, "Burn colored coins or tokens in the wallet", self.nodes[0].burntoken, self.colorids[1])
            assert_raises_rpc_error(-3, "Amount out of range", self.nodes[0].burntoken, self.colorids[1], -10)
            assert_raises_rpc_error(-3, "Invalid amount for burn", self.nodes[0].burntoken, self.colorids[1], 0)
            assert_raises_rpc_error(-8, "Invalid color parameter", self.nodes[1].burntoken, "00", 10)
            assert_raises_rpc_error(-8, "Insufficient token balance in wallet", self.nodes[1].burntoken, self.colorids[1], 10)
            assert_raises_rpc_error(-8, "Insufficient token balance in wallet", self.nodes[1].burntoken, self.colorids[2], 10)
            assert_raises_rpc_error(-8, "Insufficient token balance in wallet", self.nodes[1].burntoken, self.colorids[3], 10)

    def test_reissuetoken(self):

        self.log.info("Testing reissuetoken")
        self.nodes[0].generate(1, self.signblockprivkey_wif)
        tpc_utxo = findTPC(self.nodes[0].listunspent())

        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[0].getaddressinfo(tpc_utxo['address'])['pubkey']))
        scr =  CScript([OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ])

        res = self.nodes[0].issuetoken(1, 100, bytes_to_hex_str(scr))
        reissue_color=res['color']
        assert_equal(res['color'], self.nodes[0].getcolor(1, bytes_to_hex_str(scr)))
        assert 'address' in res
        assert_equal(self.nodes[0].getaddressinfo(res['address'])['token'], res['color'])
        self.sync_all([self.nodes[0:3]])

        res = self.nodes[0].reissuetoken(reissue_color, 100)
        assert_equal(res['color'], reissue_color)
        assert_equal(len(res['txids']), 2)
        assert 'address' in res
        assert_equal(self.nodes[0].getaddressinfo(res['address'])['token'], res['color'])

        if not self.options.usecli:
            assert_raises_rpc_error(-3, "Invalid amount", self.nodes[0].reissuetoken, self.colorids[1], 'foo')
            assert_raises_rpc_error(-3, "Invalid amount", self.nodes[0].reissuetoken, self.colorids[1], '66ae')
            assert_raises_rpc_error(-3, "Invalid amount", self.nodes[0].reissuetoken, self.colorids[1], 66.99)
            assert_raises_rpc_error(-3, "Amount out of range", self.nodes[0].reissuetoken, self.colorids[1], -10)
            assert_raises_rpc_error(-3, "Invalid token amount", self.nodes[0].reissuetoken, self.colorids[1], 0)

            assert_raises_rpc_error(-8, "Invalid color parameter", self.nodes[0].reissuetoken, self.colorids[0], 100)
            assert_raises_rpc_error(-8, "Token type not supported", self.nodes[0].reissuetoken, self.colorids[2], 100)
            assert_raises_rpc_error(-8, "Token type not supported", self.nodes[0].reissuetoken, self.colorids[3], 100)
            assert_raises_rpc_error(-8, "Token type not supported", self.nodes[0].reissuetoken, self.colorids[4], 100)
            assert_raises_rpc_error(-8, "Token type not supported", self.nodes[0].reissuetoken, self.colorids[5], 100)

            assert_raises_rpc_error(-8, "Script corresponding to color "+ self.colorids[1] +" could not be found in the wallet", self.nodes[0].reissuetoken, self.colorids[1], 100)
            assert_raises_rpc_error(-8, "Script corresponding to color "+ self.colorids[1] +" could not be found in the wallet", self.nodes[1].reissuetoken, self.colorids[1], 100)
            assert_raises_rpc_error(-8, "Script corresponding to color "+ self.colorids[1] +" could not be found in the wallet", self.nodes[2].reissuetoken, self.colorids[1], 100)
            assert_raises_rpc_error(-8, "Script corresponding to color "+ reissue_color+" could not be found in the wallet", self.nodes[1].reissuetoken, reissue_color, 100)
            assert_raises_rpc_error(-8, "Script corresponding to color "+ reissue_color+" could not be found in the wallet", self.nodes[2].reissuetoken, reissue_color, 100)

        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        self.test_nodeBalances()

    def test_issuetoken(self):

        self.log.info("Testing issuetoken")
        self.nodes[2].generate(20, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        tpc_utxo = findTPC(self.nodes[2].listunspent())
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[2].getaddressinfo(tpc_utxo['address'])['pubkey']))
        scr =  CScript([OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ])

        res1 = self.nodes[2].issuetoken(1, 100, bytes_to_hex_str(scr))
        reissue_color=res1['color']
        assert_equal(res1['color'], self.nodes[2].getcolor(1, bytes_to_hex_str(scr)))
        assert_equal(len(res1['txids']), 2)
        assert 'address' in res1
        assert_equal(self.nodes[2].getaddressinfo(res1['address'])['token'], res1['color'])
        self.sync_all([self.nodes[0:3]])

        node2_utxos = self.nodes[2].listunspent(3)

        res2 = self.nodes[2].issuetoken(2, 100, node2_utxos[1]['txid'], node2_utxos[1]['vout'])
        assert_equal(res2['color'], self.nodes[2].getcolor(2, node2_utxos[1]['txid'], node2_utxos[1]['vout']))
        assert 'address' in res2
        assert_equal(self.nodes[2].getaddressinfo(res2['address'])['token'], res2['color'])

        res3 = self.nodes[2].issuetoken(3, 1, node2_utxos[2]['txid'], node2_utxos[2]['vout'])
        assert_equal(res3['color'], self.nodes[2].getcolor(3, node2_utxos[2]['txid'], node2_utxos[2]['vout']))
        assert 'address' in res3
        assert_equal(self.nodes[2].getaddressinfo(res3['address'])['token'], res3['color'])

        res4 = self.nodes[2].issuetoken(1, 100, bytes_to_hex_str(scr))
        assert_equal(res4['color'], reissue_color)
        assert_equal(len(res4['txids']), 2)
        assert 'address' in res4
        assert_equal(self.nodes[2].getaddressinfo(res4['address'])['token'], res4['color'])

        res4 = self.nodes[2].issuetoken(1, 100, bytes_to_hex_str(scr))
        assert_equal(res4['color'], reissue_color)
        assert_equal(len(res4['txids']), 2)
        assert 'address' in res4
        assert_equal(self.nodes[2].getaddressinfo(res4['address'])['token'], res4['color'])

        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(math.floor(walletinfo['balance']['TPC']), 1456)
        assert_equal(walletinfo['balance'][res1['color']], 300)
        assert_equal(walletinfo['balance'][res2['color']], 100)
        assert_equal(walletinfo['balance'][res3['color']], 1)

        #also try reissue token
        res = self.nodes[2].reissuetoken(reissue_color, 100)
        assert_equal(res['color'], reissue_color)
        assert_equal(len(res['txids']), 2)
        assert 'address' in res
        assert_equal(self.nodes[2].getaddressinfo(res['address'])['token'], res['color'])

        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(math.floor(walletinfo['balance']['TPC']), 1506)
        assert_equal(walletinfo['balance'][res1['color']], 400)

        #negative tests
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].issuetoken, 4, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].issuetoken, 0, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].issuetoken, -1, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Extra parameter for Reissuable token", self.nodes[0].issuetoken, 1, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Parameter missing for Non-Reissuable or NFT token", self.nodes[0].issuetoken, 2, 10, node2_utxos[4]['txid'])
        assert_raises_rpc_error(-8, "Parameter missing for Non-Reissuable or NFT token", self.nodes[0].issuetoken, 3, 10, node2_utxos[4]['txid'])
        assert_raises_rpc_error(-8, "Invalid token amount for NFT. It must be 1", self.nodes[0].issuetoken, 3, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])

        if not self.options.usecli:
            assert_raises_rpc_error(-3, "Expected type number, got string", self.nodes[0].issuetoken, 2, 'foo', node2_utxos[4]['txid'], node2_utxos[4]['vout'])
            assert_raises_rpc_error(-3, "Expected type number, got string", self.nodes[0].issuetoken, 2, '66ae', node2_utxos[4]['txid'], node2_utxos[4]['vout'])
            assert_raises_rpc_error(-3, "Invalid amount", self.nodes[0].issuetoken, 2, 66.99, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
            assert_raises_rpc_error(-3, "Amount out of range", self.nodes[0].issuetoken, 2, -10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
            assert_raises_rpc_error(-3, "Invalid token amount", self.nodes[0].issuetoken, 2, 0, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
            assert_raises_rpc_error(-8, "Invalid Tapyrus script: fuy", self.nodes[0].issuetoken, 1, 100, "fuy")
            assert_raises_rpc_error(-8, "Invalid or non-wallet transaction id :aaa", self.nodes[0].issuetoken, 2, 100, "aaa", 1)
            assert_raises_rpc_error(-8, str("Invalid or non-wallet transaction id :%s" % node2_utxos[1]['txid']), self.nodes[0].issuetoken, 2, 100, node2_utxos[1]['txid'], 1)
            assert_raises_rpc_error(-8, str("Invalid or non-wallet transaction id :%s" %  res4['txids'][1]), self.nodes[0].issuetoken, 2, 100, res4['txids'][1], 10)
            assert_raises_rpc_error(-8, "Invalid vout 10 in tx: "+ node2_utxos[1]['txid'], self.nodes[2].issuetoken, 2, 100, node2_utxos[1]['txid'], 10)
            assert_raises_rpc_error(-8, str("Invalid vout 0 in tx: %s" % res1['txids'][1]), self.nodes[2].issuetoken, 2, 100, res1['txids'][1], 0)

    def test_only_token_filter(self):

        self.log.info("Testing only_token filter in listunspent")
        # all utxos
        assert_equal(len(self.nodes[0].listunspent()), 15)
        # token utxos
        assert_equal(len(self.nodes[0].listunspent(1,99999,[],False,{"only_token": True})), 6)
        # utxos with "only_token" = false  is the same as no filter
        assert_equal(len(self.nodes[0].listunspent(1,99999,[],False,{"only_token": False})), 15)

        # issue token on node 0 to create safe unconfirmed token transactions
        tpc_utxo = findTPC(self.nodes[0].listunspent())
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[0].getaddressinfo(tpc_utxo['address'])['pubkey']))
        scr1 =  CScript([OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ])

        # Disconnect nodes 0 and 1 to prevent transaction propagation before generate()
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[1], 0)

        res1 = self.nodes[0].issuetoken(1, 100, bytes_to_hex_str(scr1))
        token_unspent = len(self.nodes[0].listunspent(6,9999999,[],False,{"only_token": True}))
        assert_equal(token_unspent, 6) #unconfirmed token is not counted because of min confirmations

        # send a new token to node 0 to create unsafe token
        self.nodes[1].generate(2, self.signblockprivkey_wif)

        self.sync_all([self.nodes[0:3]])
        tpc_utxo = findTPC(self.nodes[1].listunspent())
        pubkeyhash = hash160(hex_str_to_bytes(self.nodes[1].getaddressinfo(tpc_utxo['address'])['pubkey']))
        scr2 =  CScript([OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ])

        res2 = self.nodes[1].issuetoken(1, 100, bytes_to_hex_str(scr2))

        # Reconnect nodes 0 and 1 after issuing token on node 1
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 0)
        node0_caddress = self.nodes[0].getnewaddress("", res2['color'])
        self.nodes[1].sendtoaddress(node0_caddress, 50)
        self.sync_all([self.nodes[0:3]])
        token_unspent = len(self.nodes[0].listunspent(6,9999999,[],False,{"only_token": True}))
        assert_equal(token_unspent, 6)  #unconfirmed token is not counted because of min confirmations

        #checking different combinations of min and max confirmations
        res = [7, 8, 6, 6, 1, 2, 1, 2]
        for i, options in enumerate( [[0,30,[],False], [0,30,[],True], \
                        [1,30,[],False], [1,30,[],True], \
                        [0,10,[],False], [0,10,[],True], \
                        [0,0,[],False],  [0,0,[],True] ]):

            token_unspent = self.nodes[0].listunspent(options[0],options[1],options[2],options[3],{"only_token": True})
            self.log.info(f"Options {options}: found {len(token_unspent)} tokens, expected {res[i]}")
            if len(token_unspent) != res[i]:
                self.log.info(f"Token unspents: {token_unspent}")
            assert_equal(len(token_unspent), res[i])

    def test_issuance_with_unconfirmed_change(self):
        """
        Regression tests for three related fixes in token issuance:

        1. Dynamic fallback-fee anchor fix (was hardcoded DEFAULT_FALLBACK_FEE constant):
           tx1 anchor value is now read from pwallet->m_fallback_fee.GetFeePerK()
           so it respects the node's -fallbackfee setting and is always above dust.

        2. fAllowOtherInputs=true fix:
           tx2 in IssueReissuableToken can pull in additional TPC UTXOs
           (including unconfirmed change from tx1) to cover fees.
           Before the fix, only the anchor was visible to coin
           selection → "Insufficient funds".

        3. Duplicate address book entry fix:
           Multiple reissues of the same REISSUABLE color previously created a
           new CColorScriptID address book entry per call.  The total balance
           appeared multiplied in the GUI.  After the fix only one entry exists
           per color, and the balance is the correct sum.
        """
        self.log.info("Testing issuance with unconfirmed change and duplicate-entry guard")

        # Start node 3 and join the existing network so it learns the chain.
        # Use sync_blocks only: node 3 starts with an empty mempool and will
        # not receive the existing unconfirmed txs from nodes 0-2, so a full
        # sync_all would time out on mempool sync.
        self.start_node(3)
        connect_nodes_bi(self.nodes, 0, 3)
        sync_blocks(self.nodes[0:4])

        # Mine exactly 1 block on node 3.  This gives it ONE confirmed coinbase
        # UTXO (50 TPC).  Once the first issuance spends it, the wallet holds
        # only unconfirmed change — the scenario that exposed fixes 1 & 2.
        self.nodes[3].generate(1, self.signblockprivkey_wif)
        sync_blocks(self.nodes[0:4])

        # Build the P2PKH script that will define the REISSUABLE color.
        tpc_utxo = findTPC(self.nodes[3].listunspent(1))
        pubkeyhash = hash160(
            hex_str_to_bytes(self.nodes[3].getaddressinfo(tpc_utxo['address'])['pubkey']))
        scr = CScript([OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG])
        scr_hex = bytes_to_hex_str(scr)

        # issue then reissue without mining a block between ---
        # The issuetoken call consumes the only confirmed UTXO; after it the
        # wallet has only unconfirmed change from tx2.
        res1 = self.nodes[3].issuetoken(1, 100, scr_hex)
        assert_equal(len(res1['txids']), 2)
        color1 = res1['color']
        assert 'address' in res1
        assert_equal(self.nodes[3].getaddressinfo(res1['address'])['token'], color1)

        # reissuetoken must use that unconfirmed change.
        res2 = self.nodes[3].reissuetoken(color1, 50)
        assert_equal(res2['color'], color1)
        assert_equal(len(res2['txids']), 2)
        assert 'address' in res2
        assert_equal(self.nodes[3].getaddressinfo(res2['address'])['token'], color1)

        # A third reissue (still no confirmed UTXOs) must also succeed.
        res3 = self.nodes[3].reissuetoken(color1, 25)
        assert_equal(res3['color'], color1)
        assert_equal(len(res3['txids']), 2)
        assert 'address' in res3
        assert_equal(self.nodes[3].getaddressinfo(res3['address'])['token'], color1)

        # NON_REISSUABLE using an unconfirmed TPC UTXO ---
        # listunspent(0) includes mempool UTXOs (minconf=0).
        # The RPC accepts an explicit unconfirmed UTXO as the color-defining
        all_utxos = self.nodes[3].listunspent(0)
        unconf_tpc = next(
            (u for u in all_utxos
             if u.get('confirmations', 1) == 0 and u['token'] == 'TPC' and u['spendable']),
            None)
        assert unconf_tpc is not None, "Expected at least one unconfirmed TPC UTXO after issuance"

        res4 = self.nodes[3].issuetoken(2, 75, unconf_tpc['txid'], unconf_tpc['vout'])
        assert 'txid' in res4  # NON_REISSUABLE returns a single 'txid' string, not a 'txids' list
        assert 'address' in res4
        assert_equal(self.nodes[3].getaddressinfo(res4['address'])['token'], res4['color'])

        # Mine one block to confirm everything, then verify final balances.
        # Earlier, because of duplicate address book entries, color1's balance would
        # appear as a multiple of the true sum instead of 100+50+25 = 175.
        self.nodes[3].generate(1, self.signblockprivkey_wif)

        walletinfo = self.nodes[3].getwalletinfo()
        assert_equal(walletinfo['balance'][color1], 175)   # 100 + 50 + 25
        assert_equal(walletinfo['balance'][res4['color']], 75)

    def run_test(self):
        # Check that there's no UTXO on any of the nodes
        assert_equal(len(self.nodes[0].listunspent()), 0)
        assert_equal(len(self.nodes[1].listunspent()), 0)
        assert_equal(len(self.nodes[2].listunspent()), 0)

        self.log.info("Mining blocks...")

        self.nodes[0].generate(6, self.signblockprivkey_wif)
        assert_equal(self.nodes[0].getbalance(), 300)

        total_unspent = len(self.nodes[0].listunspent())
        if self.options.usecli:
            token_unspent = len(self.nodes[0].listunspent(query_options="{\"only_token\": true}"))
        else:
            token_unspent = len(self.nodes[0].listunspent(query_options={"only_token": True}))
        assert_equal(total_unspent, 6)
        assert_equal(token_unspent, 0)

        self.sync_all([self.nodes[0:3]])
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        self.test_nodeBalances()

        utxos = self.nodes[0].listunspent()

        self.test_getcolorRPC(utxos)
        self.test_createrawtransaction(utxos)
        self.test_sendRPC('sendtoaddress')
        self.test_sendRPC('transfertoken')
        self.test_burntoken()
        self.test_reissuetoken()
        self.test_issuetoken()
        if not self.options.usecli:
            self.test_only_token_filter()
        self.test_issuance_with_unconfirmed_change()


reverse_bytes = (lambda txid  : txid[-1: -len(txid)-1: -1])

if __name__ == '__main__':
    WalletColoredCoinTest().main()
