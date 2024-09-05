#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool limiting together/eviction with the wallet.
    extend this test to test mempool eviction with packages"""

import time
from io import BytesIO
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_raises_rpc_error, bytes_to_hex_str, hex_str_to_bytes
from test_framework.messages import CTransaction, CTxIn, CTxOut, COutPoint, COIN, ToHex
from test_framework.script import CScript, OP_TRUE, MAX_SCRIPT_ELEMENT_SIZE
from test_framework.address import key_to_p2pkh

MAX_SCRIPT_SIZE = 10000
MAX_BIP125_RBF_SEQUENCE = 0xfffffffd

NUM_OUTPUTX_IN_LARGE_TX = 5

tx_size = lambda tx : len(ToHex(tx))//2 + 120 * len(tx.vin) + 50

class MempoolLimitTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [["-maxmempool=5"]] * self.num_nodes

    def deduct_fee(self, tx, feerate):
        tx.vout[0].nValue -= int(feerate / 1000 * tx_size(tx) * COIN)

    def setup_network(self):
        self.setup_nodes()

    def create_tx_with_large_script(self, prevtx, n, count, amt):
        # Create a large transaction with 'count' outputs
        # all outputs of this tx have the largest 'OP_RETURN' script of the format accepted by tapyrus
        # none of these are spendable
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(prevtx, n), b"", 0xffffffff))
        current_size = 0
        script_output = CScript([b''])
        while MAX_SCRIPT_SIZE - current_size  > MAX_SCRIPT_ELEMENT_SIZE:
            script_output = script_output + CScript([b'\x6a', b'\x51' * (MAX_SCRIPT_ELEMENT_SIZE - 5) ])
            current_size = current_size + MAX_SCRIPT_ELEMENT_SIZE + 1
        for i in range(count):
            tx.vout.append(CTxOut(int(amt * COIN), script_output))
        tx.rehash()
        return tx

    def create_utxos(self, node, count, fee):
        # Use a Freah node with default mempool minimum feerate
        assert_equal(node.getmempoolinfo()['minrelaytxfee'], Decimal('0.00001000'))
        assert_equal(node.getmempoolinfo()['mempoolminfee'], Decimal('0.00001000'))

        node.generate(count, self.signblockprivkey_wif)
        utxos = [utxo for utxo in node.listunspent() if utxo['amount'] > fee]
        return utxos

    def fill_mempool(self, node, utxos, feerate, num_tx=97, nSequence=0):
        # fill the mempool with a maximum of 'num_tx' large transactions
        # making sure that there is still space for the next big transaction
        txids = []
        self.log.info("Fill mempool")
        info = node.getmempoolinfo()
        utxo = utxos.pop()
        tx = self.create_tx_with_large_script(int(utxo['txid'], 16), utxo['vout'], NUM_OUTPUTX_IN_LARGE_TX, (utxo['amount'])/NUM_OUTPUTX_IN_LARGE_TX)
        cnt = 0
        while info['maxmempool'] - info['usage'] > tx_size(tx):
            utxo = utxos.pop()
            tx = self.create_tx_with_large_script(int(utxo['txid'], 16), utxo['vout'], NUM_OUTPUTX_IN_LARGE_TX, (utxo['amount'])/NUM_OUTPUTX_IN_LARGE_TX)
            tx.vin[0].nSequence = nSequence
            self.deduct_fee(tx, feerate)
            signresult = node.signrawtransactionwithwallet(ToHex(tx))
            txid = node.sendrawtransaction(signresult["hex"])
            txids.append(txid)
            info = node.getmempoolinfo()
            cnt += 1
            if cnt >= num_tx:
                break
        return txids

    def create_signed_raw_tx(self, node, spend_utxo, feerate):
        # Create transaction without fee so that its accurate size can be calculated
        raw_tx =node.createrawtransaction(
                inputs=[{'txid': spend_utxo['txid'], 'vout': spend_utxo['vout']}],
                outputs=[{self.address: spend_utxo['amount']}])
        # Pay fee according to transaction size
        tx = CTransaction()
        tx.deserialize(BytesIO(hex_str_to_bytes(raw_tx)))
        self.deduct_fee(tx, feerate)
        tx.rehash()
        # sign transaction (do not send here, sent in the package)
        raw_tx = node.signrawtransactionwithwallet(tx.serialize().hex(), [{'txid' : spend_utxo['txid'], 'vout' : spend_utxo['vout'], 'scriptPubKey' : spend_utxo['scriptPubKey']}], "ALL", self.options.scheme)
        assert_equal(raw_tx["complete"], True)
        return (tx, raw_tx['hex'])

    def create_package(self, node, utxos, mempool_evicted_utxo, feerate, size=5, large=False):
        # create size-1 parent transactions and one child transaction spend all parents
        # one of them should spend the mempool_evicted_utxo
        self.log.info("Create package transactions")
        parent_txs = []
        package_hex =[]
        package_txids = []
        inputs = []
        prevtx = []
        value = 0

        if mempool_evicted_utxo is not None:
            spend_utxos = [mempool_evicted_utxo] + utxos[:size-2]
        else:
            spend_utxos = utxos[:size-1]

        # create parent transactions
        for parent_spend in spend_utxos:
            (parent_tx, parent_hex) = self.create_signed_raw_tx(node, parent_spend, feerate)
            self.log.debug("parent tx: %s spending %s" % (parent_tx.hashMalFix, parent_spend['txid']))
            parent_txs.append(parent_tx)
            package_hex.append(parent_hex)
            package_txids.append(parent_tx.hashMalFix)
            inputs.append({'txid': parent_tx.hashMalFix, 'vout': 0})
            prevtx.append({'txid':parent_tx.hashMalFix, 'scriptPubKey': bytes_to_hex_str(parent_tx.vout[0].scriptPubKey), 'vout':0, 'amount':parent_tx.vout[0].nValue/COIN})
            value += parent_tx.vout[0].nValue

        # Create a child transaction spending everything.
        if large:
            # child with large scriptpubkey
            utxo = utxos.pop()
            child = self.create_tx_with_large_script(int(utxo['txid'], 16), utxo['vout'], 1, utxo['amount'])
            for parent in parent_txs:
                child.vin.append(CTxIn(COutPoint(parent.malfixsha256, 0)))
        else:
            raw_child =node.createrawtransaction(
                    inputs= inputs,
                    outputs=[{self.address: value / COIN}])
            child = CTransaction()
            child.deserialize(BytesIO(hex_str_to_bytes(raw_child)))
        self.deduct_fee(child, feerate)
        child.rehash()
        raw_child = node.signrawtransactionwithwallet(child.serialize().hex(), prevtx, "ALL", self.options.scheme)
        assert_equal(raw_child["complete"], True)
        package_hex.append(raw_child['hex'])
        package_txids.append(child.hashMalFix)
        self.log.info("child : %s" % child.hashMalFix)
        return (package_hex, package_txids)

    def create_tx_to_be_evicted(self, node, unspent, feerate):
        # Mempool transaction which is evicted due to being at the "bottom" of the mempool when the
        # mempool overflows and evicts by descendant score.

        inputs = [{"txid": unspent["txid"], "vout": unspent["vout"]}]
        outputs = [{key_to_p2pkh(self.signblockpubkey): unspent['amount'] - feerate}]
        raw_tx = node.createrawtransaction(inputs, outputs)
        signresult = node.signrawtransactionwithwallet(raw_tx,  [{'txid' : unspent['txid'], 'vout' : unspent["vout"], 'scriptPubKey' : unspent['scriptPubKey']}], "ALL", self.options.scheme)
        assert_equal(signresult["complete"], True)
        mempool_evicted_txid = node.sendrawtransaction(signresult['hex'], True)

        # make sure the tx is in mempool
        assert mempool_evicted_txid in node.getrawmempool()
        mempool_evicted_tx = CTransaction()
        mempool_evicted_tx.deserialize(BytesIO(hex_str_to_bytes(signresult['hex'])))
        assert_equal(mempool_evicted_txid, mempool_evicted_tx.rehash())
        return (mempool_evicted_tx, signresult['hex'])

    def test_package_full_mempool(self, node):
        # this test verifies that during package submission if the mempool becomes full
        # submission of some transactions within the package succeed
        self.log.info("A package is partially submitted upon submission if the mempool is full")
        mempoolmin_feerate = node.getmempoolinfo()["mempoolminfee"]
        relayfee = node.getnetworkinfo()['relayfee']
        utxos = self.create_utxos(node, 120, mempoolmin_feerate * 100)

        # fill the mempool with large transactions with verh fee so that they are not chosen for eviction
        self.fill_mempool(node, utxos, mempoolmin_feerate * 200, nSequence=0, num_tx=98)


        # There is a filler transaction. It is large so that most of the mempool is filled
        # this makes sure that the package submittted next would
        # fill the mempool when evaluation is midway
        utxo = utxos.pop()
        tx = self.create_tx_with_large_script(int(utxo['txid'], 16), utxo['vout'], 1, utxo['amount'])
        tx.vin[0].nSequence = 0
        self.deduct_fee(tx, mempoolmin_feerate * 100)
        signresult = node.signrawtransactionwithwallet(ToHex(tx))
        txid = node.sendrawtransaction(signresult["hex"])
        assert txid in node.getrawmempool()

        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate]

        (package_hex, package_txids) = self.create_package(node, utxos, None, mempoolmin_feerate * 10, size=20, large=True)

        # Package should be submitted partially
        mempool_txids = node.getrawmempool()

        res = self.submitpackage(node, package_hex)

        # Failed midway due to full mempool
        success = False
        for x in [True for txid in package_txids if res[txid] == {'allowed': False, 'reject-reason': '66: mempool full'}]:
            success = success | x
        assert success

        # Maximum size must never be exceeded.
        assert_greater_than(node.getmempoolinfo()["maxmempool"], node.getmempoolinfo()["bytes"])

        # some package transactions are in mempool.
        resulting_mempool_txids = node.getrawmempool()
        assert(mempool_txids != resulting_mempool_txids)

    def test_mid_package_eviction(self, node):
        self.log.info("Check a package where each parent passes the current mempoolminfee but would cause eviction before package submission terminates")
        relayfee = node.getnetworkinfo()['relayfee']
        mempoolmin_feerate = node.getmempoolinfo()["mempoolminfee"]
        utxos = self.create_utxos(node, 103, relayfee)

        # fill the mempool with large transactions
        self.fill_mempool(node, utxos, mempoolmin_feerate * 200)

        self.log.info("Send transaction to be evicted from mempool")
        (mempool_evicted_tx, mempool_evicted_hex) = self.create_tx_to_be_evicted(node, utxos.pop(), 1)
        mempool_evicted_utxo = {'txid': mempool_evicted_tx.hashMalFix,
                                'vout': 0,
                                'scriptPubKey' : bytes_to_hex_str(mempool_evicted_tx.vout[0].scriptPubKey),
                                'amount' : mempool_evicted_tx.vout[0].nValue / COIN
                                }

        # There is a filler transaction. It is large so that most of the mempool is filled
        # this makes sure that the package submittted next would trigger eviction
        self.log.info("Fill the mempool with another large transaction")
        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  relayfee * 10]
        utxo = utxos.pop()
        tx = self.create_tx_with_large_script(int(utxo['txid'], 16), utxo['vout'], 4, utxo['amount']/4)
        self.deduct_fee(tx, mempoolmin_feerate * 100)
        signresult = node.signrawtransactionwithwallet(ToHex(tx))
        node.sendrawtransaction(signresult["hex"], True)

        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate * 100]
        init_mempool_txids = node.getrawmempool()

        (package_hex, txids) = self.create_package(node, utxos, mempool_evicted_utxo, relayfee, large=True)

        self.log.info("Submit package transactions")
        res = self.submitpackage(node, package_hex)

        for txid in txids:
            assert_equal(res[txid], {'allowed':True})

        # Maximum size must never be exceeded.
        assert_greater_than(node.getmempoolinfo()["maxmempool"], node.getmempoolinfo()["bytes"])

        # Evicted transaction and its descendants must not be in mempool.
        self.log.info("Verify eviction")
        resulting_mempool_txids = node.getrawmempool()
        actual_mempool_evicted_tx = [tx for tx in init_mempool_txids if tx not in resulting_mempool_txids]
        assert actual_mempool_evicted_tx not in resulting_mempool_txids

        if mempool_evicted_tx == actual_mempool_evicted_tx:
            for txid in txids:
                assert txid not in resulting_mempool_txids
        else:
            for txid in txids:
                assert txid in resulting_mempool_txids

    def test_mid_package_replacement(self, node):
        self.log.info("Check a package where an early tx depends on a later-replaced mempool tx")

        relayfee = node.getnetworkinfo()['relayfee'] * 10
        utxos = self.create_utxos(node, 103, relayfee)

        # fill the mempool with large transactions
        self.fill_mempool(node, utxos, relayfee)

        current_info = node.getmempoolinfo()
        mempoolmin_feerate = current_info["mempoolminfee"]

        # Mempool transaction which is evicted due to being at the "bottom" of the mempool when the
        # mempool overflows and evicts by descendant score.
        double_spent_utxo = utxos.pop()
        inputs = [{"txid": double_spent_utxo["txid"], "vout": double_spent_utxo["vout"], "sequence":MAX_BIP125_RBF_SEQUENCE}]
        outputs = [{self.address: double_spent_utxo['amount']}]
        replaced_tx_hex = node.createrawtransaction(inputs, outputs)
        replaced_tx = CTransaction()
        replaced_tx.deserialize(BytesIO(hex_str_to_bytes(replaced_tx_hex)))
        self.deduct_fee(replaced_tx, 10 * mempoolmin_feerate)
        replaced_tx.rehash()
        replaced_tx_hex = node.signrawtransactionwithwallet(replaced_tx.serialize().hex(), [double_spent_utxo], "ALL", self.options.scheme)
        assert_equal(replaced_tx_hex["complete"], True)
        txid = node.sendrawtransaction(replaced_tx_hex['hex'])
        self.log.debug("sent replaced_tx: %s", txid)

        # Already in mempool when package is submitted.
        assert txid in node.getrawmempool()

        # This parent spends the above mempool transaction that exists when its inputs are first
        # looked up, but disappears later. It is rejected for being too low fee (but eligible for
        # reconsideration), and its inputs are cached. When the mempool transaction is evicted, its
        # coin is no longer available, but the cache could still contain the tx.
        replaced_tx_utxo = {"txid": replaced_tx.hashMalFix, "vout": 0, "amount": '{:.8f}'.format(Decimal(replaced_tx.vout[0].nValue/COIN)), "scriptPubKey":bytes_to_hex_str(replaced_tx.vout[0].scriptPubKey)}
        inputs = [{"txid": replaced_tx_utxo["txid"], "vout": replaced_tx_utxo["vout"], "sequence":MAX_BIP125_RBF_SEQUENCE}]
        outputs = [{self.address: replaced_tx_utxo['amount']}]
        pkg_parent = node.createrawtransaction(inputs, outputs)
        pkg_parent_tx = CTransaction()
        pkg_parent_tx.deserialize(BytesIO(hex_str_to_bytes(pkg_parent)))
        self.deduct_fee(pkg_parent_tx, 10 * mempoolmin_feerate)
        txid = pkg_parent_tx.rehash()
        pkg_parent = node.signrawtransactionwithwallet(pkg_parent_tx.serialize().hex(), [replaced_tx_utxo], "ALL", self.options.scheme)
        assert_equal(pkg_parent["complete"], True)
        self.log.debug("package pkg_parent: %s", txid)

        # Tx that replaces the parent of pkg_parent.
        inputs = [{"txid": double_spent_utxo["txid"], "vout": double_spent_utxo["vout"], "sequence":MAX_BIP125_RBF_SEQUENCE}]
        outputs = [{self.address: double_spent_utxo['amount']}]
        replacement_tx_hex = node.createrawtransaction(inputs, outputs)
        replacement_tx = CTransaction()
        replacement_tx.deserialize(BytesIO(hex_str_to_bytes(replacement_tx_hex)))
        self.deduct_fee(replacement_tx, 100 * mempoolmin_feerate)
        txid = replacement_tx.rehash()
        replacement_tx_hex = node.signrawtransactionwithwallet(replacement_tx.serialize().hex(), [double_spent_utxo], "ALL", self.options.scheme)
        assert_equal(replacement_tx_hex["complete"], True)
        self.log.debug("package replacement_tx: %s", txid)

        # Create a child spending both
        script = hex_str_to_bytes(node.getaddressinfo(self.address)['scriptPubKey'])
        child = CTransaction()
        child.vin.append(CTxIn(COutPoint(pkg_parent_tx.malfixsha256, 0), b"", MAX_BIP125_RBF_SEQUENCE))
        child.vin.append(CTxIn(COutPoint(replacement_tx.malfixsha256, 0), b"", MAX_BIP125_RBF_SEQUENCE))
        child.vout.append(CTxOut(int(pkg_parent_tx.vout[0].nValue + replacement_tx.vout[0].nValue), script))
        self.deduct_fee(child, 20 * mempoolmin_feerate)
        txid = child.rehash()
        child_hex = node.signrawtransactionwithwallet(child.serialize().hex(), [{'txid':pkg_parent_tx.hashMalFix, 'vout':0, 'scriptPubKey': bytes_to_hex_str(pkg_parent_tx.vout[0].scriptPubKey), 'amount':pkg_parent_tx.vout[0].nValue/COIN}, {'txid':replacement_tx.hashMalFix, 'vout':0, 'scriptPubKey': bytes_to_hex_str(replacement_tx.vout[0].scriptPubKey), 'amount':replacement_tx.vout[0].nValue/COIN}], "ALL", self.options.scheme)
        assert_equal(child_hex["complete"], True)
        self.log.debug("package child: %s", txid)

        # It's very important that the pkg_parent is before replacement_tx so that its input (from
        # replaced_tx) is first looked up *before* replacement_tx is submitted.
        package_hex = [pkg_parent['hex'], replacement_tx_hex['hex'], child_hex['hex']]

        # Package should be submitted, temporarily exceeding maxmempool, and then evicted.
        self.log.info("submit package")
        res = self.submitpackage(node, package_hex)

        # pkg_parent_tx is accepted first
        assert_equal(res[pkg_parent_tx.hashMalFix], {'allowed': True})

        # pkg_parent_tx and replaced_tx are replaced
        assert_equal(res[replacement_tx.hashMalFix], {'allowed': True} )

        # pkg_parent_tx is missing
        assert_equal(res[child.hashMalFix], {'allowed': False, 'reject-reason': 'missing-inputs'} )

        # Maximum size must never be exceeded.
        assert_greater_than(node.getmempoolinfo()["maxmempool"], node.getmempoolinfo()["bytes"])

        self.log.info("Verify package replacement")
        resulting_mempool_txids = node.getrawmempool()

        # The replacement take place.
        assert replaced_tx.hashMalFix not in resulting_mempool_txids

        # package submission is partial
        assert replacement_tx.hashMalFix in resulting_mempool_txids
        assert pkg_parent_tx.hashMalFix not in resulting_mempool_txids
        assert child.hashMalFix not in resulting_mempool_txids

    def test_mempool_eviction(self, node):
        relayfee = node.getnetworkinfo()['relayfee']
        utxos = self.create_utxos(node, 103, relayfee)

        self.log.info('Create a mempool tx that will be evicted')
        us0 = utxos.pop()
        inputs = [{ "txid" : us0["txid"], "vout" : us0["vout"]}]
        outputs = {self.address : 0.0001}
        tx = node.createrawtransaction(inputs, outputs)
        node.settxfee(relayfee) # specifically fund this tx with low fee
        txF = node.fundrawtransaction(tx)
        node.settxfee(0) # return to automatic fee selection
        txFS = node.signrawtransactionwithwallet(txF['hex'], [], "ALL", self.options.scheme)
        txid = node.sendrawtransaction(txFS['hex'])
        assert(txid in node.getrawmempool())

        # fill the mempool with large transactions
        self.fill_mempool(node, utxos, relayfee)
        assert(txid in node.getrawmempool())

        # send one last transaction that will fill the rest of the mempool
        utxo = [utxo for utxo in node.listunspent()if utxo['amount'] >  relayfee * 10000][0]
        tx = self.create_tx_with_large_script(int(utxo['txid'], 16), utxo['vout'], 2, (utxo['amount'])/3)
        info = node.getmempoolinfo()
        while tx_size(tx) < info['maxmempool'] - info['usage']:
            info = node.getmempoolinfo()
            tx.vout.append(CTxOut(1 * COIN, tx.vout[0].scriptPubKey))
        self.deduct_fee(tx, relayfee)
        tx.vout[0].nValue -= len(tx.vout) * COIN
        signresult = node.signrawtransactionwithwallet(ToHex(tx))
        node.sendrawtransaction(signresult["hex"], True)

        self.log.info('The tx should be evicted by now')
        assert(txid not in node.getrawmempool())
        txdata = node.gettransaction(txid)
        assert(txdata['confirmations'] ==  0) #confirmation should still be 0

        self.log.info('Check that mempoolminfee is larger than minrelytxfee')
        assert_equal(node.getmempoolinfo()['minrelaytxfee'], Decimal('0.00001000'))
        assert_greater_than(node.getmempoolinfo()['mempoolminfee'], Decimal('0.00001000'))

        self.log.info('Create a mempool tx that will not pass mempoolminfee')
        us0 = node.listunspent()[0]
        inputs = [{ "txid" : us0["txid"], "vout" : us0["vout"]}]
        outputs = {self.address : 0.0001}
        tx = node.createrawtransaction(inputs, outputs)
        # specifically fund this tx with a fee < mempoolminfee, >= than minrelaytxfee
        txF = node.fundrawtransaction(tx, {'feeRate': relayfee})
        txFS = node.signrawtransactionwithwallet(txF['hex'], [], "ALL", self.options.scheme)
        assert_raises_rpc_error(-26, "mempool min fee not met", node.sendrawtransaction, txFS['hex'])

        node.generate(1, self.signblockprivkey_wif)

    def run_test(self):
        self.address = key_to_p2pkh(self.signblockpubkey)

        for node in self.nodes:
            node.importprivkey(self.signblockprivkey_wif)

        self.log.info("Test passing a value below the minimum (5 MB) to -maxmempool throws an error")
        self.stop_node(0)
        self.restart_node(0, extra_args=self.extra_args[0])
        self.nodes[0].assert_start_raises_init_error(["-maxmempool=4"], "Error: -maxmempool must be at least 5 MB")

        self.log.info("Phase 1 : Mempool eviction")
        self.nodes[0].generate(1, self.signblockprivkey_wif)
        self.test_mempool_eviction(self.nodes[0])

        self.log.info("Phase 2 : Package Mempool eviction")
        self.nodes[1].generate(1, self.signblockprivkey_wif)
        self.test_mid_package_eviction(self.nodes[1])

        self.log.info("Phase 3 : Other Package Mempool test")
        self.test_package_full_mempool(self.nodes[2])
        self.test_mid_package_replacement(self.nodes[3])

    def submitpackage(self, node, package_hex):
        # Package should be submitted with enough logging.
        mempool_1 = node.getrawmempool()
        info_1 = node.getmempoolinfo()

        res = node.submitpackage(package_hex, True)

        mempool_2 = node.getrawmempool()
        info_2 = node.getmempoolinfo()
        diff = [tx for tx in mempool_2 if tx not in mempool_1]
        evict = [tx for tx in mempool_1 if tx not in mempool_2]
        self.log.info("\n\n%s\n\n" % res)

        self.log.info("Mempool info : \n%s\n%s\n\n" % (info_1, info_2))
        #self.log.info("Mempool add : %s\n" % diff)
        self.log.info("Mempool evict : %s\n" % evict)

        return res

if __name__ == '__main__':
    MempoolLimitTest().main()
