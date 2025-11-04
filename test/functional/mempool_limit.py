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
from test_framework.script import CScript, OP_TRUE, MAX_SCRIPT_ELEMENT_SIZE, MAX_SCRIPT_SIZE
from test_framework.address import key_to_p2pkh

MAX_BIP125_RBF_SEQUENCE = 0xfffffffd
TX_SIZE_TOFILL_MEMPOOL = 5000000 / 100

NUM_OUTPUTX_IN_LARGE_TX = 5

tx_size = lambda tx : len(ToHex(tx))//2 + 120 * len(tx.vin) + 50
size_package = lambda package_hex: sum(len(tx) for tx in package_hex)

class MempoolLimitTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 4
        self.extra_args = [["-maxmempool=5"]] * self.num_nodes

    def deduct_fee(self, tx, feerate):
        tx.vout[0].nValue -= int(feerate / 1000 * tx_size(tx) * COIN)

    def setup_network(self):
        self.setup_nodes()

    def create_tx_with_large_script(self, utxo, feerate, size_needed=None):
        # Create a large transaction with 'count' outputs
        # all outputs of this tx have the largest 'OP_RETURN' script of the format accepted by tapyrus
        # none of these are spendable
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(utxo['txid'], 16), utxo['vout']), b"", MAX_BIP125_RBF_SEQUENCE))
        current_size = 0
        script_output = CScript([b''])
        while MAX_SCRIPT_SIZE - current_size  > MAX_SCRIPT_ELEMENT_SIZE:
            script_output = script_output + CScript([b'\x6a', b'\x51' * (MAX_SCRIPT_ELEMENT_SIZE - 5) ])
            current_size += MAX_SCRIPT_ELEMENT_SIZE + 1
        tx.vout.append(CTxOut(0, script_output))
        while True:
            size_now = tx_size(tx)
            if size_needed - size_now > len(script_output):
                tx.vout.append(CTxOut(0, script_output))
            elif size_now > size_needed or size_needed - size_now <= 10:
                break
            else:
                tx.vout.append(CTxOut(0, CScript([b'\x6a', b'\x51' * (MAX_SCRIPT_ELEMENT_SIZE - 5) ])))
        fee = int(feerate / 1000 * tx_size(tx) * COIN)
        tx_amt = int((utxo['amount'] * COIN - fee)/len(tx.vout))
        for out in tx.vout:
            out.nValue = tx_amt
        tx.rehash()
        return tx

    def fill_mempool(self, node, utxos, feerate, nSequence=0):
        # fill the mempool with large transactions
        # making sure that there is still space for the next transaction
        txids = []
        self.log.info("Fill mempool")
        while True:
            info = node.getmempoolinfo()
            utxo = utxos.pop()
            size_needed = TX_SIZE_TOFILL_MEMPOOL
            tx = self.create_tx_with_large_script(utxo, feerate, size_needed)
            tx.vin[0].nSequence = nSequence
            signresult = node.signrawtransactionwithwallet(ToHex(tx))
            txid = node.sendrawtransaction(signresult["hex"], True)
            txids.append(txid)
            info = node.getmempoolinfo()
            if info['maxmempool'] - info['usage'] < tx_size(tx):
                break
            elif info['maxmempool'] - info['usage'] < TX_SIZE_TOFILL_MEMPOOL:
                size_needed = info['maxmempool'] - info['usage']
            else:
                size_needed = TX_SIZE_TOFILL_MEMPOOL
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

    def create_package(self, node, utxos, mempool_evicted_utxo, feerate, size=5):
        # create size-1 parent transactions and one child transaction spending all parents
        # one of the parents should spend the mempool_evicted_utxo if it is given
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
        return (package_hex, package_txids)

    def create_tx_to_be_evicted(self, node, unspent, feerate=None):
        # Mempool transaction which is evicted due to being at the "bottom" of the mempool when the
        # mempool overflows and evicts by descendant score.

        if feerate is None:
            feerate = node.getmempoolinfo()["mempoolminfee"] * Decimal('1.01')

        # Create transaction without fee first to calculate accurate size
        inputs = [{"txid": unspent["txid"], "vout": unspent["vout"]}]
        outputs = [{key_to_p2pkh(self.signblockpubkey): unspent['amount']}]
        raw_tx = node.createrawtransaction(inputs, outputs)

        # Deserialize to calculate actual fee
        tx = CTransaction()
        tx.deserialize(BytesIO(hex_str_to_bytes(raw_tx)))
        tx.rehash()

        # Sign to get actual size
        signresult = node.signrawtransactionwithwallet(raw_tx,  [{'txid' : unspent['txid'], 'vout' : unspent["vout"], 'scriptPubKey' : unspent['scriptPubKey']}], "ALL", self.options.scheme)
        assert_equal(signresult["complete"], True)

        # Parse signed transaction to get accurate size and apply precise fee
        mempool_evicted_tx = CTransaction()
        mempool_evicted_tx.deserialize(BytesIO(hex_str_to_bytes(signresult['hex'])))
        tx_size_bytes = len(hex_str_to_bytes(signresult['hex']))

        # Calculate precise fee: feerate is in BTC/kB, convert to satoshis
        fee = int(feerate * tx_size_bytes / 1000 * COIN)

        # Recreate transaction with precise fee
        outputs = [{key_to_p2pkh(self.signblockpubkey): unspent['amount'] - Decimal(fee) / COIN}]
        raw_tx = node.createrawtransaction(inputs, outputs)
        signresult = node.signrawtransactionwithwallet(raw_tx,  [{'txid' : unspent['txid'], 'vout' : unspent["vout"], 'scriptPubKey' : unspent['scriptPubKey']}], "ALL", self.options.scheme)
        assert_equal(signresult["complete"], True)
        mempool_evicted_txid = node.sendrawtransaction(signresult['hex'])

        # make sure the tx is in mempool
        assert mempool_evicted_txid in node.getrawmempool()
        mempool_evicted_tx = CTransaction()
        mempool_evicted_tx.deserialize(BytesIO(hex_str_to_bytes(signresult['hex'])))
        assert_equal(mempool_evicted_txid, mempool_evicted_tx.rehash())
        return (mempool_evicted_tx, signresult['hex'])

    def test_package_full_mempool(self, node):
        # this test verifies that during package submission if the mempool becomes full
        # and the fee of the later tx is not large enough to cause replacement
        # submission of some transactions within the package succeeds

        self.log.info("A package is partially submitted if its fee cannot trigger transaction replacement in mempool")
        relayfee = node.getnetworkinfo()['relayfee']
        mempoolmin_feerate = node.getmempoolinfo()["mempoolminfee"]
        assert_equal(relayfee, Decimal('0.00001000'))
        assert_equal(mempoolmin_feerate, Decimal('0.00001000'))
        node.generate(120, self.signblockprivkey_wif)
        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate * 100]

        # fill the mempool with large transactions with very high fee so that they are not chosen for eviction
        self.fill_mempool(node, utxos, mempoolmin_feerate * 200, nSequence=0)

        #create package to calculate its size. do not submit now
        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate]
        (package_hex, package_txids) = self.create_package(node, utxos, None, mempoolmin_feerate, size=10)

        # fill the rest of the mempool with another transaction
        mempoolinfo = node.getmempoolinfo()
        # Reserve slightly less space than package size to ensure some txs will be rejected
        size_needed = mempoolinfo['maxmempool'] - mempoolinfo['usage'] - int(size_package(package_hex))

        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate]
        utxo = utxos.pop()
        tx = self.create_tx_with_large_script(utxo, mempoolmin_feerate * 100, size_needed)
        tx.vin[0].nSequence = 0
        signresult = node.signrawtransactionwithwallet(ToHex(tx))
        txid = node.sendrawtransaction(signresult["hex"])

        mempool_txids = node.getrawmempool()
        assert txid in mempool_txids

        # Verify mempool is nearly full
        mempoolinfo_after = node.getmempoolinfo()
        self.log.info("Mempool usage after fill: {} / {} bytes".format(
            mempoolinfo_after['usage'], mempoolinfo_after['maxmempool']))

        # Package should be submitted partially
        res = self.submitpackage(node, package_hex)

        self.log.info("Verify partial submission")
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
        for txid in package_txids:
            if res[txid]['allowed']:
                assert txid in resulting_mempool_txids

    def test_mid_package_eviction(self, node):
        # this test verifies that during package submission if the mempool becomes full
        # and the fee of package transactions are high then some low fee transaction is evicted.
        # If one of the transactions in the package spends the evicted transaction output
        # the package is not fully submitted

        self.log.info("Check a package is not accepted if it spends a transaction which is evicted")
        relayfee = node.getnetworkinfo()['relayfee']
        mempoolmin_feerate = node.getmempoolinfo()["mempoolminfee"]
        assert_equal(relayfee, Decimal('0.00001000'))
        assert_equal(mempoolmin_feerate, Decimal('0.00001000'))
        node.generate(110, self.signblockprivkey_wif)
        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate * 100]

        # fill the mempool with large transactions
        self.fill_mempool(node, utxos, mempoolmin_feerate * 200)

        self.log.info("Send transaction with low fee")
        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate * 100]
        (mempool_evicted_tx, mempool_evicted_hex) = self.create_tx_to_be_evicted(node, utxos.pop())
        mempool_evicted_utxo = {'txid': mempool_evicted_tx.hashMalFix,
                                'vout': 0,
                                'scriptPubKey' : bytes_to_hex_str(mempool_evicted_tx.vout[0].scriptPubKey),
                                'amount' : mempool_evicted_tx.vout[0].nValue / COIN
                                }

        #create package to calculate its size. do not submit now
        (package_hex, package_txids) = self.create_package(node, utxos, mempool_evicted_utxo, mempoolmin_feerate * 200, size=6)

        # fill the rest of the mempool with a filler transaction
        mempoolinfo = node.getmempoolinfo()
        size_needed = mempoolinfo['maxmempool'] - mempoolinfo['usage'] - size_package(package_hex)

        self.log.info("Fill the mempool with another large transaction")
        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  relayfee * 10]
        utxo = utxos.pop()
        tx = self.create_tx_with_large_script(utxo, mempoolmin_feerate * 100, size_needed)
        self.deduct_fee(tx, mempoolmin_feerate * 100)
        signresult = node.signrawtransactionwithwallet(ToHex(tx))
        node.sendrawtransaction(signresult["hex"], True)

        init_mempool_txids = node.getrawmempool()

        # Submit package
        res = self.submitpackage(node, package_hex)

        # Child trasnaction in the package failed due to one of its inputs being evicted and some inputs disappearing
        assert res[package_txids[-1] ] == {'allowed': False, 'reject-reason': 'missing-inputs'}

        # Maximum size must never be exceeded.
        assert_greater_than(node.getmempoolinfo()["maxmempool"], node.getmempoolinfo()["bytes"])

        # some package transactions are in mempool.
        resulting_mempool_txids = node.getrawmempool()
        assert(init_mempool_txids != resulting_mempool_txids)

        # Evicted transaction results in one of the package transactions being rejected.
        self.log.info("Verify that the low fee transction is evicted")
        resulting_mempool_txids = node.getrawmempool()
        actual_mempool_evicted_txs = [tx for tx in init_mempool_txids if tx not in resulting_mempool_txids]

        # Log detailed information for debugging
        #self.log.info(f"Expected evicted tx: {mempool_evicted_tx.hashMalFix}")
        #self.log.info(f"Actually evicted txs: {actual_mempool_evicted_txs}")

        # The low fee transaction should be among the evicted transactions
        assert mempool_evicted_tx.hashMalFix in actual_mempool_evicted_txs, \
            f"Expected {mempool_evicted_tx.hashMalFix} to be evicted, but evicted txs were: {actual_mempool_evicted_txs}"


    def test_mid_package_replacement(self, node):
        # this test cheks that package transactions which replace other mempool transactions are
        # identified correctly and other package transaction which may spend the replaced transaction
        # are rejected

        self.log.info("Check a package where an early tx depends on a later-replaced mempool tx")
        relayfee = node.getnetworkinfo()['relayfee']
        mempoolmin_feerate = node.getmempoolinfo()["mempoolminfee"]
        assert_equal(relayfee, Decimal('0.00001000'))
        assert_equal(mempoolmin_feerate, Decimal('0.00001000'))
        node.generate(110, self.signblockprivkey_wif)
        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate * 100]

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
        self.log.info('Create a mempool tx that will be evicted')
        relayfee = node.getnetworkinfo()['relayfee']
        mempoolmin_feerate = node.getmempoolinfo()["mempoolminfee"]

        assert_equal(relayfee, Decimal('0.00001000'))
        assert_equal(mempoolmin_feerate, Decimal('0.00001000'))

        node.generate(103, self.signblockprivkey_wif)
        utxos = [utxo for utxo in node.listunspent()if utxo['amount'] >  mempoolmin_feerate * 100]

        us0 = utxos.pop()
        inputs = [{ "txid" : us0["txid"], "vout" : us0["vout"]}]
        outputs = {self.address : 0.0001}
        tx = node.createrawtransaction(inputs, outputs)
        node.settxfee(relayfee) # specifically fund this tx with low fee
        txF = node.fundrawtransaction(tx)
        node.settxfee(0) # return to automatic fee selection
        txFS = node.signrawtransactionwithwallet(txF['hex'], [], "ALL", self.options.scheme)
        tx_o = CTransaction()
        tx_o.deserialize(BytesIO(hex_str_to_bytes(txFS['hex'])))
        txid = node.sendrawtransaction(txFS['hex'] ,True)
        assert(txid in node.getrawmempool())

        # fill the mempool with large transactions
        self.fill_mempool(node, utxos, relayfee)
        assert(txid in node.getrawmempool())

        mempoolinfo = node.getmempoolinfo()
        size_needed = mempoolinfo['maxmempool'] - mempoolinfo['usage'] - tx_size(tx_o)

        # send one last transaction that will fill the rest of the mempool
        utxo = [utxo for utxo in node.listunspent()if utxo['amount'] >  relayfee * 10000][0]
        tx = self.create_tx_with_large_script(utxo, relayfee, size_needed)
        signresult = node.signrawtransactionwithwallet(ToHex(tx))
        node.sendrawtransaction(signresult["hex"], True)

        # Wait for mempool eviction to process
        time.sleep(0.1)

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
        self.test_mempool_eviction(self.nodes[0])

        self.log.info("Phase 2 : Mempool eviction - high fee package")
        self.test_mid_package_eviction(self.nodes[1])

        self.log.info("Phase 3 : Mempool eviction - low fee package")
        self.test_package_full_mempool(self.nodes[2])

        self.log.info("Phase 4 : Mempool eviction - package triggering mempool replacement")
        self.test_mid_package_replacement(self.nodes[3])

    def submitpackage(self, node, package_hex, debug = False):
        # Package should be submitted with enough logging.
        
        mempool_1 = node.getrawmempool()
        info_1 = node.getmempoolinfo()

        res = node.submitpackage(package_hex, True)

        mempool_2 = node.getrawmempool()
        info_2 = node.getmempoolinfo()

        if debug:
            diff = [tx for tx in mempool_2 if tx not in mempool_1]
            evict = [tx for tx in mempool_1 if tx not in mempool_2]
            self.log.info("\n\n%s\n\n" % res)

            self.log.info("Mempool info : \n%s\n%s\n\n" % (info_1, info_2))
            self.log.info("Mempool add : %s\n" % diff)
            self.log.info("Mempool evict : %s\n" % evict)

        return res

if __name__ == '__main__':
    MempoolLimitTest().main()
