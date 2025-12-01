#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool acceptance of raw transactions."""

from io import BytesIO
import copy,  random
import time
from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import (
    BIP125_SEQUENCE_NUMBER,
    COIN,
    CTransaction,
    MAX_BLOCK_BASE_SIZE,
    CTxIn,
    CTxOut,
    COutPoint,
    uint256_from_str,
)
from test_framework.script import (
    OP_0,
    OP_RESERVED,
    OP_EQUAL,
    OP_HASH160,
    OP_RETURN,
)
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    bytes_to_hex_str,
    hex_str_to_bytes,
    wait_until,
)
from test_framework.address import key_to_p2pkh
from test_framework.key import CECKey
from test_framework.blocktools import createTestGenesisBlock
from test_framework.mininode  import P2PDataStore

class RPCPackageTest(BitcoinTestFramework):
    MAX_PACKAGE_COUNT = 25

    def __init(self):
        self.signblockprivkey = CECKey()
        self.coinbase_key.set_secretbytes(bytes.fromhex("8d5366123cb560bb606379f90a0bfd4769eecc0557f1b362dcae9012b548b1e5"))
        self.signblockpubkey = self.coinbase_key.get_pubkey()
        self.setup_clean_chain = True
        self.genesisBlock = createTestGenesisBlock(self.coinbase_pubkey, self.coinbase_key, int(time.time() - 100))

    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [[
            '-txindex',
        ]] * self.num_nodes

    def check_submit_mempool_result(self, result_expected, *args, **kwargs):
        """Wrapper to check result of submitpackage rpc on node_0's mempool"""
        result_test = self.nodes[0].submitpackage(*args, **kwargs)
        assert_equal(result_expected, result_test)
        assert_equal(self.nodes[0].getmempoolinfo()['size'], self.mempool_size + len(kwargs['rawtxs']))

    def check_mempool_result(self, result_expected, *args, **kwargs):
        """Wrapper to check result of testmempoolaccept rpc on node_0's mempool"""
        result_test = self.nodes[0].testmempoolaccept(*args, **kwargs)
        assert_equal(result_expected, result_test)
        assert_equal(self.nodes[0].getmempoolinfo()['size'], self.mempool_size)

    def create_package(self,  size):
        """create a package with size transactions"""
        self.nodes[0].generate(1, self.signblockprivkey_wif)

        package  = []
        #first tx spends a coin in the blockchain
        # Sort UTXOs deterministically to avoid race conditions
        available_coins = [x for x in self.nodes[0].listunspent() if x['amount'] > 49.3]
        available_coins.sort(key=lambda x: (x['txid'], x['vout']))
        coin = available_coins[0]
        prevtx_hex=self.nodes[0].getrawtransaction(coin['txid'])
        prevtx = CTransaction()
        prevtx.deserialize(BytesIO(hex_str_to_bytes(prevtx_hex)))
        prevtxs = [{"txid": coin['txid'],
                        "vout": coin['vout'],
                        "scriptPubKey": bytes_to_hex_str(prevtx.vout[coin['vout']].scriptPubKey),
                        "redeemScript": "",
                        "amount": prevtx.vout[coin['vout']].nValue  /  COIN}]
        raw_tx = self.nodes[0].createrawtransaction(
            inputs=[{'txid': coin['txid'], 'vout': coin['vout']}],
            outputs=[{self.nodes[0].getnewaddress(): 0.3}, {self.nodes[0].getnewaddress(): 49}]
        )
        signed_raw_tx =self.nodes[0].signrawtransactionwithwallet(raw_tx, prevtxs)['hex']
        tx = CTransaction()
        tx.deserialize(BytesIO(hex_str_to_bytes(signed_raw_tx)))
        package.insert(0, tx)
        prevtx  = tx
        amt = 48.5
        # other txs spend the output of the previous tx in the package.
        for x in range(2, size+1):
            coin = {'txid': tx.rehash(), 'vout':1}
            prevtxs = [{"txid": coin['txid'],
                                "vout": coin['vout'],
                                "scriptPubKey": bytes_to_hex_str(prevtx.vout[coin['vout']].scriptPubKey),
                                "redeemScript": "",
                                "amount": prevtx.vout[coin['vout']].nValue  /  COIN}]
            raw_tx = self.nodes[0].createrawtransaction(
                inputs=[{'txid': coin['txid'], 'vout': coin['vout']}],
                outputs=[{self.nodes[0].getnewaddress(): 0.3}, {self.nodes[0].getnewaddress(): amt}]
            )
            signed_raw_tx =self.nodes[0].signrawtransactionwithwallet(raw_tx, prevtxs)['hex']
            amt = amt - 0.5
            tx = CTransaction()
            tx.deserialize(BytesIO(hex_str_to_bytes(signed_raw_tx)))
            package.insert(x-1, tx)
            prevtx  = tx
        [x.rehash() for x in package]
        return package

    def run_test(self):
        node = self.nodes[0]

        self.log.info('Start with empty mempool, and 10 blocks')
        self.mempool_size = 0
        node.generate(10, self.signblockprivkey_wif)
        assert_equal(node.getmempoolinfo()['size'], self.mempool_size)

        self.log.info('Test package acceptance')
        # package with 26 transactions is rejected
        package = self.create_package(26)
        raw_package = [bytes_to_hex_str(x.serialize()) for x in package]

        assert_raises_rpc_error(-8, "Too many transactions in package", node.testmempoolaccept, raw_package)

        # package with txs spending the same input is rejected
        package = self.create_package(5)
        package[1].vin[0].prevout = package[0].vin[0].prevout
        package[3].vin[0].prevout = package[2].vin[0].prevout
        raw_package = [bytes_to_hex_str(x.serialize()) for x in package]

        self.check_mempool_result(
            result_expected={'allowed': False, 'reject-reason': '69: conflict-in-package'},
            rawtxs=raw_package,
            )

        # unsorted package is rejected
        package = self.create_package(3)
        package[1].vin[0].prevout.hash = package[2].malfixsha256
        raw_package = [bytes_to_hex_str(x.serialize()) for x in package]

        self.check_mempool_result(
            result_expected={'allowed': False, 'reject-reason': '69: package-not-sorted'},
            rawtxs=raw_package,
        )

        # Packages can't have a total size of more than MAX_PACKAGE_COUNT(25) * 1000
        self.log.info('Test very large package')
        package_too_large = []
        node.generate(200, self.signblockprivkey_wif)
        total_size = 0
        while total_size <= self.MAX_PACKAGE_COUNT * 1000:
            tx = CTransaction()
            coins = [x for x in self.nodes[0].listunspent()]
            # Sort UTXOs deterministically to avoid race conditions
            coins.sort(key=lambda x: (x['txid'], x['vout']))
            tx.vin = [CTxIn(COutPoint(uint256_from_str(hex_str_to_bytes(coins[i]['txid'])), int(coins[i]['vout'])), b"", 0xffffffff) for i in range(0, 25)]
            tx.vout = [CTxOut(100, b"")]
            total_size += len(tx.serialize())
            package_too_large.append(tx)
        raw_package = [bytes_to_hex_str(x.serialize()) for x in package_too_large]

        self.check_mempool_result(
            result_expected={'allowed': False, 'reject-reason': '69: package-too-large'},
            rawtxs=raw_package,
            allowhighfees=True
        )

        # package with unknown utxo is rejected
        package = self.create_package(2)
        package[1].vin[0].prevout.n = 6
        package[1].rehash()
        raw_package = [bytes_to_hex_str(x.serialize()) for x in package]

        self.mempool_size = 1
        self.check_mempool_result(
            result_expected={ package[0].hashMalFix: {'allowed': True},
                                            package[1].hashMalFix: {'allowed': False, 'reject-reason': 'missing-inputs'}},
            rawtxs=raw_package,
            allowhighfees=True
        )

        # package tx with high fee is rejected
        package = self.create_package(2)
        package[1].vin[0].prevout.n = 6
        package[1].rehash()
        raw_package = [bytes_to_hex_str(x.serialize()) for x in package]

        self.mempool_size = 0
        self.check_mempool_result(
            result_expected={ package[0].hashMalFix: {'allowed': False, 'reject-reason': '256: absurdly-high-fee'},
                                            package[1].hashMalFix: {'allowed': False, 'reject-reason': 'missing-inputs'}},
            rawtxs=raw_package
        )

        # package is accepted
        package = self.create_package(4)
        raw_package = [bytes_to_hex_str(x.serialize()) for x in package]

        self.mempool_size = 4
        self.check_mempool_result(
            result_expected={ package[0].hashMalFix: {'allowed': True},
                                            package[1].hashMalFix: {'allowed': True},
                                            package[2].hashMalFix: {'allowed': True},
                                            package[3].hashMalFix: {'allowed': True}},
            rawtxs=raw_package,
            allowhighfees=True
        )


        # package is accepted
        package = self.create_package(1)
        raw_package = [bytes_to_hex_str(x.serialize()) for x in package]

        self.mempool_size = 1
        self.check_mempool_result(
            result_expected={ package[0].hashMalFix: {'allowed': True} },
            rawtxs=raw_package,
            allowhighfees=True
        )

        self.log.info('Test colored coins in package')
        # test colored coins in package
        node.generate(6, self.signblockprivkey_wif)

        utxos = node.listunspent()
        # Filter UTXOs to ensure they have sufficient amounts for the transactions (need at least 11+ TPC each)
        # Sort by amount descending first to get the largest UTXOs, then by txid/vout for determinism
        sufficient_utxos = [x for x in utxos if x['amount'] >= 12.0]  # 12 to ensure enough for fees
        sufficient_utxos.sort(key=lambda x: (-x['amount'], x['txid'], x['vout']))
        
        if len(sufficient_utxos) < 3:
            raise Exception(f"Need at least 3 UTXOs with >= 12 TPC, but only found {len(sufficient_utxos)}")
        
        utxos = sufficient_utxos
        addr = key_to_p2pkh(self.signblockpubkey)
    
        # Reissuable
        colorid1 = node.getcolor(1, utxos[0]['scriptPubKey'])
        cp2pkh_address1 = key_to_p2pkh(self.signblockpubkey, color=hex_str_to_bytes(colorid1))

        # issue
        issue_tx_1 = node.signrawtransactionwithwallet(node.createrawtransaction(
                inputs=[{'txid': utxos[0]['txid'], 'vout': utxos[0]['vout']}],
                outputs=[{addr: 10}, {node.getnewaddress() : 1}, { cp2pkh_address1 : 1000}],
            ), [], "ALL", self.options.scheme)['hex']
        i_tx_1 = CTransaction()
        i_tx_1.deserialize(BytesIO(hex_str_to_bytes(issue_tx_1)))
        i_tx_1.rehash()
        # spend
        spend_tx_1 = node.createrawtransaction(
                inputs=[{'txid': i_tx_1.hashMalFix, 'vout': 2},  {'txid': i_tx_1.hashMalFix, 'vout': 0}],
                outputs=[{node.getnewaddress("", colorid1): 200}, {node.getnewaddress(): 9}])
        spend_tx_1 = node.signrawtransactionwithkey(spend_tx_1, [self.signblockprivkey_wif], [{'txid' : i_tx_1.hashMalFix, 'vout' : 2, 'scriptPubKey' : bytes_to_hex_str(i_tx_1.vout[2].scriptPubKey)}, {'txid' : i_tx_1.hashMalFix, 'vout' : 0, 'scriptPubKey' : bytes_to_hex_str(i_tx_1.vout[0].scriptPubKey)} ], "ALL", self.options.scheme)['hex']
        s_tx_1 = CTransaction()
        s_tx_1.deserialize(BytesIO(hex_str_to_bytes(spend_tx_1)))
        s_tx_1.rehash()

        # Non-Reissuable
        colorid2 = node.getcolor(2, utxos[1]['txid'], utxos[1]['vout'])
        cp2pkh_address2 = key_to_p2pkh(self.signblockpubkey, color=hex_str_to_bytes(colorid2))
        # issue
        issue_tx_2 = node.signrawtransactionwithwallet(node.createrawtransaction(
                inputs=[{'txid': utxos[1]['txid'], 'vout': utxos[1]['vout']}],
                outputs=[{addr: 10}, {node.getnewaddress() : 1}, { cp2pkh_address2 : 100}],
            ), [], "ALL", self.options.scheme)['hex']
        i_tx_2 = CTransaction()
        i_tx_2.deserialize(BytesIO(hex_str_to_bytes(issue_tx_2)))
        i_tx_2.rehash()
        # spend
        spend_tx_2 = node.createrawtransaction(
                inputs=[{'txid': i_tx_2.hashMalFix, 'vout': 2}, {'txid': i_tx_2.hashMalFix, 'vout': 0}],
                outputs=[{node.getnewaddress("", colorid2): 50}, {node.getnewaddress(): 9}])
        spend_tx_2 = node.signrawtransactionwithkey(spend_tx_2, [self.signblockprivkey_wif], [{'txid' : i_tx_2.hashMalFix, 'vout' : 2, 'scriptPubKey' : bytes_to_hex_str(i_tx_2.vout[2].scriptPubKey)}, {'txid' : i_tx_2.hashMalFix, 'vout' : 0, 'scriptPubKey' : bytes_to_hex_str(i_tx_2.vout[0].scriptPubKey)} ], "ALL", self.options.scheme)['hex']
        s_tx_2 = CTransaction()
        s_tx_2.deserialize(BytesIO(hex_str_to_bytes(spend_tx_2)))
        s_tx_2.rehash()

        # NFT
        colorid3 = node.getcolor(3, utxos[2]['txid'], utxos[2]['vout'])
        cp2pkh_address3 = key_to_p2pkh(self.signblockpubkey, color=hex_str_to_bytes(colorid3))
        # issue
        issue_tx_3 = node.signrawtransactionwithwallet(node.createrawtransaction(
                inputs=[{'txid': utxos[2]['txid'], 'vout': utxos[2]['vout']}],
                outputs=[{addr: 10}, {node.getnewaddress() : 1}, { cp2pkh_address3 : 1}],
            ), [], "ALL", self.options.scheme)['hex']
        i_tx_3 = CTransaction()
        i_tx_3.deserialize(BytesIO(hex_str_to_bytes(issue_tx_3)))
        i_tx_3.rehash()
        # spend
        spend_tx_3 =node.createrawtransaction(
                inputs=[{'txid': i_tx_3.hashMalFix, 'vout': 2}, {'txid': i_tx_3.hashMalFix, 'vout': 0}],
                outputs=[{node.getnewaddress("", colorid3): 1}, {node.getnewaddress(): 8}])
        spend_tx_3 = node.signrawtransactionwithkey(spend_tx_3, [self.signblockprivkey_wif], [{'txid' : i_tx_3.hashMalFix, 'vout' : 2, 'scriptPubKey' : bytes_to_hex_str(i_tx_3.vout[2].scriptPubKey)}, {'txid' : i_tx_3.hashMalFix, 'vout' : 0, 'scriptPubKey' : bytes_to_hex_str(i_tx_3.vout[0].scriptPubKey)} ], "ALL", self.options.scheme)['hex']
        s_tx_3 = CTransaction()
        s_tx_3.deserialize(BytesIO(hex_str_to_bytes(spend_tx_3)))
        s_tx_3.rehash()

        package = [issue_tx_1, issue_tx_2, issue_tx_3, spend_tx_1, spend_tx_2, spend_tx_3]

        txids = []
        for i in package:
            packagetx = CTransaction()
            packagetx.deserialize(BytesIO(hex_str_to_bytes(i)))
            packagetx.rehash()
            txids.append(packagetx.hashMalFix)

        self.mempool_size = len(txids)
        self.check_mempool_result(
            result_expected={ txids[0]: {'allowed': True},
                                            txids[1]: {'allowed': True},
                                            txids[2]: {'allowed': True},
                                            txids[3]: {'allowed': True},
                                            txids[4]: {'allowed': True},
                                            txids[5]: {'allowed': True}},
            rawtxs=package,
            allowhighfees=True
        )

if __name__ == '__main__':
    RPCPackageTest().main()
