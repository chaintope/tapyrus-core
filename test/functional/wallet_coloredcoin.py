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
        createrawtransaction
        sendtoaddress


    """
from codecs import encode
import decimal
from time import sleep

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_array_result,
    assert_equal,
    assert_fee_amount,
    connect_nodes_bi,
    hex_str_to_bytes,
    assert_raises_rpc_error
)
from test_framework.messages import sha256
from test_framework.script import CScript, hash160, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG
from test_framework.address import byte_to_base58

class WalletColoredCoinTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.extra_args = [["-minrelaytxfee=0.00001"],["-minrelaytxfee=0.00001"],["-minrelaytxfee=0.00001"], []]

        self.pubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc"]

        self.privkeys = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37","dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b"]

        # this is the complete list of all colorids generated in this test.
        # this helps in balance verification using the below list
        self.colorids = []

        # this is the complete balance info of nodes 0, 1 and 2
        # level represents the state in the test case when balance is verified
        # first member of each level is the total number of tokens in the wallet (including TPC)
        self.balance_expected = [
                     [ #'''nodes 0'''
                        [ 1, 300],                                #level 0
                        [ 4, 267, 100, 100, 1],                   #level 1 create part1
                        [ 6, 234, 200, 100, 1, 100, 1],           #level 2 create part2
                        [ 6, "233.9999506", 180, 90, 0, 90, 0],   #level 3 sendtoaddress
                        [ 6, "233.9999126", 160, 80, 0, 80, 0],   #level 4 transfertoken
                        [ 6, "233.9998881", 140, 60, 0, 60, 0],   #level 5 burn partial
                        [ 6, "233.9998881", 140, 60, 0, 60, 0],   #level 6 burn full
                      ], 
                     [ #'''nodes 1'''
                        [ 0, 0],                     #level 0
                        [ 4, 30, 0, 0, 0],           #level 1
                        [ 6, 60, 0, 0, 0, 0, 0],     #level 2
                        [ 6, 60, 20, 10, 1, 10, 1],  #level 3
                        [ 6, 60, 40, 20, 1, 20, 1],  #level 4
                        [ 6, 60, 40, 20, 1, 20, 1],  #level 5
                        [ 4, "59.9999595", 0, 0, 0], #level 6
                      ],
                     [ #'''nodes 2'''
                        [ 1, 50],                 #level 0
                        [ 1, 103],                #level 1
                        [ 1, 156],                #level 2
                        [ 1, "206.0000494"],      #level 3
                        [ 1, "256.0000874"],      #level 4
                        [ 1, "306.0000874"],      #level 5
                        [ 1, "356.0001119"],      #level 6
                      ] 
        ]

    #class variable to remember the stage of testing
    level = 0

    def test_nodeBalances(self):
        ''' check all node balances in one place:'''
        #context = decimal.getcontext()
        #context.prec = 6

        for i in [0, 1, 2]: #node
            walletinfo = self.nodes[i].getwalletinfo()
            assert_equal(len(walletinfo['balance']), self.balance_expected[i][self.level][0])
            assert_equal(round(self.nodes[i].getbalance(), 7), decimal.Decimal(self.balance_expected[i][self.level][1]))
            for j in range(2, self.balance_expected[i][self.level][0]): #colorid
                assert_equal(walletinfo['balance'][self.colorids[j-1]], self.balance_expected[i][self.level][j])
        self.level += 1

    def setup_network(self):
        self.add_nodes(4)
        self.start_node(0) 
        self.start_node(1) 
        self.start_node(2)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        self.sync_all([self.nodes[0:3]])
        relayfee = self.nodes[0].getnetworkinfo()['relayfee']
        self.nodes[0].settxfee(2 * relayfee)
        self.nodes[1].settxfee(2 * relayfee)
        self.nodes[2].settxfee(2 * relayfee)

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
        raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': utxos[5]['txid'], 'vout': utxos[5]['vout']}],
            outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2pkh_address1 : 100}],
        ), [], "ALL", self.options.scheme)['hex']
        self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        #  create transaction 2 with colorid2
        raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': utxos[1]['txid'], 'vout': utxos[1]['vout']}],
            outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2pkh_address2 : 100}],
        ), [], "ALL", self.options.scheme)['hex']
        self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        #  create transaction 3 with colorid3
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
        raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': utxos[0]['txid'], 'vout': utxos[0]['vout']}],
            outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2sh_address1 : 100}],
        ), [], "ALL", self.options.scheme)['hex']
        self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        #  create transaction 2 with colorid4
        raw_tx_in_block = self.nodes[0].signrawtransactionwithwallet(self.nodes[0].createrawtransaction(
            inputs=[{'txid': utxos[3]['txid'], 'vout': utxos[3]['vout']}],
            outputs=[{self.nodes[1].getnewaddress(): 10}, {self.nodes[0].getnewaddress() : 39}, { cp2sh_address4 : 100}],
        ), [], "ALL", self.options.scheme)['hex']
        self.nodes[0].sendrawtransaction(hexstring=raw_tx_in_block, allowhighfees=True)

        #  create transaction 3 with colorid5
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
                            {"txid": txid1},
                            {"category": "send",
                            "token" : self.colorids[1],
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid1},
                            {"category": "receive",
                            "token" : self.colorids[1],
                            "amount": 10,
                            "confirmations": 0})

        txid2 = sendrpc(cp2sh_address1, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid2},
                            {"category": "send",
                            "token" : self.colorids[1],
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid2},
                            {"category": "receive",
                            "token" : self.colorids[1],
                            "amount": 10,
                            "confirmations": 0})

        txid3 = sendrpc(cp2pkh_address2, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid3},
                            {"category": "send",
                            "token" : self.colorids[2],
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid3},
                            {"category": "receive",
                            "token" : self.colorids[2],
                            "amount": 10,
                            "confirmations": 0})

        #NFT can be transferred only once. so call it only the first time
        if(rpcname == "sendtoaddress"):
            txid4 = sendrpc(cp2pkh_address3, 1)
            self.sync_all([self.nodes[0:3]])

            assert_array_result(self.nodes[0].listtransactions(),
                                {"txid": txid4},
                                {"category": "send",
                                "token" : self.colorids[3],
                                "amount": -1,
                                "confirmations": 0})
            assert_array_result(self.nodes[1].listtransactions(),
                                {"txid": txid4},
                                {"category": "receive",
                                "token" : self.colorids[3],
                                "amount": 1,
                                "confirmations": 0})

        txid5 = sendrpc(cp2sh_address4, 10)
        self.sync_all([self.nodes[0:3]])

        assert_array_result(self.nodes[0].listtransactions(),
                            {"txid": txid5},
                            {"category": "send",
                            "token" : self.colorids[4],
                            "amount": -10,
                            "confirmations": 0})
        assert_array_result(self.nodes[1].listtransactions(),
                            {"txid": txid5},
                            {"category": "receive",
                            "token" : self.colorids[4],
                            "amount": 10,
                            "confirmations": 0})

        #NFT can be transferred only once. so call it only the first time
        if(rpcname == "sendtoaddress"):
            txid6 = sendrpc(cp2sh_address5, 1)
            self.sync_all([self.nodes[0:3]])

            assert_array_result(self.nodes[0].listtransactions(),
                                {"txid": txid6},
                                {"category": "send",
                                "token" : self.colorids[5],
                                "amount": -1,
                                "confirmations": 0})
            assert_array_result(self.nodes[1].listtransactions(),
                                {"txid": txid6},
                                {"category": "receive",
                                "token" : self.colorids[5],
                                "amount": 1,
                                "confirmations": 0})
        #mine a block, confirmations should change:
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        txlist = self.nodes[0].listtransactions()
        assert_array_result(txlist,{"txid": txid1},{"confirmations": 1})
        assert_array_result(txlist,{"txid": txid2},{"confirmations": 1})
        assert_array_result(txlist,{"txid": txid3},{"confirmations": 1})
        assert_array_result(txlist,{"txid": txid5},{"confirmations": 1})

        if(rpcname == "sendtoaddress"):
            assert_array_result(txlist,{"txid": txid4},{"confirmations": 1})
            assert_array_result(txlist,{"txid": txid6},{"confirmations": 1})

        self.test_nodeBalances()

    def test_burntoken(self):
        self.log.info("Testing burntoken")
        #partial
        self.nodes[0].burntoken(self.colorids[1], 20)
        self.nodes[0].burntoken(self.colorids[2], 20)
        self.nodes[0].burntoken(self.colorids[4], 20)

        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])

        self.test_nodeBalances()

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
        assert_raises_rpc_error(-1, "Burn colored coins or tokens in the wallet", self.nodes[0].burntoken, "00")
        assert_raises_rpc_error(-1, "Burn colored coins or tokens in the wallet", self.nodes[0].burntoken, "c4")
        assert_raises_rpc_error(-1, "Burn colored coins or tokens in the wallet", self.nodes[0].burntoken, self.colorids[1])
        assert_raises_rpc_error(-3, "Invalid amount for burn", self.nodes[0].burntoken, self.colorids[1], -10)
        assert_raises_rpc_error(-5, "No Token found in wallet. But token address was given.", self.nodes[1].burntoken, self.colorids[1], 10)
        assert_raises_rpc_error(-5, "No Token found in wallet. But token address was given.", self.nodes[1].burntoken, self.colorids[2], 10)
        assert_raises_rpc_error(-5, "No Token found in wallet. But token address was given.", self.nodes[1].burntoken, self.colorids[3], 10)

    def test_issuetoken(self):

        self.log.info("Testing issuetoken")
        self.nodes[2].generate(20, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])
        node2_utxos = self.nodes[2].listunspent()

        # Lock UTXO so nodes[2] doesn't accidentally spend it
        self.nodes[2].lockunspent(False, [{"txid": node2_utxos[0]['txid'], "vout": node2_utxos[0]['vout']}])

        res1 = self.nodes[2].issuetoken(1, 100, node2_utxos[0]['scriptPubKey'])
        assert_equal(res1['color'], self.nodes[2].getcolor(1, node2_utxos[0]['scriptPubKey']))

        res2 = self.nodes[2].issuetoken(2, 100, node2_utxos[1]['txid'], node2_utxos[1]['vout'])
        assert_equal(res2['color'], self.nodes[2].getcolor(2, node2_utxos[1]['txid'], node2_utxos[1]['vout']))

        res3 = self.nodes[2].issuetoken(3, 1, node2_utxos[2]['txid'], node2_utxos[2]['vout'])
        assert_equal(res3['color'], self.nodes[2].getcolor(3, node2_utxos[2]['txid'], node2_utxos[2]['vout']))

        self.nodes[2].issuetoken(1, 100, node2_utxos[0]['scriptPubKey'])
        assert_equal(res1['color'], self.nodes[2].getcolor(1, node2_utxos[0]['scriptPubKey']))

        self.nodes[2].issuetoken(1, 100, node2_utxos[0]['scriptPubKey'])
        assert_equal(res1['color'], self.nodes[2].getcolor(1, node2_utxos[0]['scriptPubKey']))

        sleep(15)
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        self.sync_all([self.nodes[0:3]])
        sleep(30)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(walletinfo['balance']['TPC'], decimal.Decimal('1406.00015236'))
        assert_equal(walletinfo['balance'][res1['color']], 300)
        assert_equal(walletinfo['balance'][res2['color']], 100)
        assert_equal(walletinfo['balance'][res3['color']], 1)

        #negative tests
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].issuetoken, 4, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].issuetoken, 0, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Unknown token type given", self.nodes[0].issuetoken, -1, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Invalid token amount", self.nodes[0].issuetoken, 2, -10, node2_utxos[4]['txid'], -1)
        assert_raises_rpc_error(-8, "Extra parameter for Reissuable token", self.nodes[0].issuetoken, 1, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Parameter missing for Non-Reissuable or NFT token", self.nodes[0].issuetoken, 2, 10, node2_utxos[4]['txid'])
        assert_raises_rpc_error(-8, "Parameter missing for Non-Reissuable or NFT token", self.nodes[0].issuetoken, 3, 10, node2_utxos[4]['txid'])
        assert_raises_rpc_error(-8, "Invalid token amount", self.nodes[0].issuetoken, 2, -10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])
        assert_raises_rpc_error(-8, "Invalid token amount for NFT. It must be 1", self.nodes[0].issuetoken, 3, 10, node2_utxos[4]['txid'], node2_utxos[4]['vout'])

    def run_test(self):
        # Check that there's no UTXO on any of the nodes
        assert_equal(len(self.nodes[0].listunspent()), 0)
        assert_equal(len(self.nodes[1].listunspent()), 0)
        assert_equal(len(self.nodes[2].listunspent()), 0)

        self.log.info("Mining blocks...")

        self.nodes[0].generate(6, self.signblockprivkey_wif)
        assert_equal(self.nodes[0].getbalance(), 300)

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
        self.test_issuetoken()


reverse_bytes = (lambda txid  : txid[-1: -len(txid)-1: -1])

if __name__ == '__main__':
    WalletColoredCoinTest().main()
