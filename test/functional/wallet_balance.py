#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the wallet balance RPC methods."""
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.blocktools import create_colored_transaction
from test_framework.util import assert_equal

RANDOM_COINBASE_ADDRESS = 'mneYUmWYsuk7kySiURxCi3AGxrAqZxLgPZ'

def create_transactions(node, color, address, amt, fees):
    # Create and sign raw transactions from node to address for amt.
    # Creates a transaction for each fee and returns an array
    # of the raw transactions.
    all_utxos = node.listunspent(0)
    utxos = [utxo for utxo in all_utxos if utxo['token'] == color]
    all_utxos = [utxo for utxo in all_utxos if utxo not in utxos and utxo['token'] == 'TPC']
    # Create transactions
    inputs = []
    ins_total = 0
    max_amt = amt
    for utxo in utxos:
        inputs.append({"txid": utxo["txid"], "vout": utxo["vout"]})
        ins_total += utxo['amount']
        if color == 'TPC':
            max_amt = amt + max(fees)
        if ins_total >= max_amt:
            break
    # make sure there was enough utxos
    assert ins_total >= max_amt

    txs = []
    first = True
    for fee in fees:
        if color == 'TPC':
            outputs = {address: amt}
            # prevent 0 change output
            if ins_total > amt + fee:
                outputs[node.getrawchangeaddress()] = ins_total - amt - fee
        else:
            if first:
                fee_in = [utxo for utxo in all_utxos if utxo['safe']]
                inputs.append({"txid": fee_in[0]["txid"], "vout": fee_in[0]["vout"]}) #fee
            first = False
            outputs = {}
            outputs[node.getrawchangeaddress()] = fee_in[0]['amount'] - fee #fee change
            outputs[node.getrawchangeaddress(color)] = ins_total - amt
        raw_tx = node.createrawtransaction(inputs, outputs, 0, True)
        raw_tx = node.signrawtransactionwithwallet(raw_tx)
        txs.append(raw_tx)
    return txs

class WalletTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        # Check that nodes don't own any UTXOs
        assert_equal(len(self.nodes[0].listunspent()), 0)
        assert_equal(len(self.nodes[1].listunspent()), 0)

        self.log.info("Mining one block for each node")

        self.nodes[0].generate(1, self.signblockprivkey_wif)
        colorid1 = create_colored_transaction(2, 100, self.nodes[0])['color']
        self.nodes[0].generatetoaddress(1, RANDOM_COINBASE_ADDRESS, self.signblockprivkey_wif)
        self.sync_all()
        self.nodes[1].generate(1, self.signblockprivkey_wif)
        colorid2 = create_colored_transaction(2, 100, self.nodes[1])['color']
        self.nodes[1].generatetoaddress(1, RANDOM_COINBASE_ADDRESS, self.signblockprivkey_wif)
        self.sync_all()

        assert_equal(self.nodes[0].getbalance(), Decimal('49.99995520'))
        wallet_bal = self.nodes[0].getwalletinfo()['balance']
        assert_equal(wallet_bal[colorid1], 100)
        assert_equal(self.nodes[1].getbalance(), Decimal('49.99995520'))
        wallet_bal = self.nodes[1].getwalletinfo()['balance']
        assert_equal(wallet_bal[colorid2], 100)

        self.log.info("Test getbalance with different arguments")
        assert_equal(self.nodes[0].getbalance(True), Decimal('49.99995520'))
        assert_equal(self.nodes[1].getbalance(True), Decimal('49.99995520'))

        # Send 40 TPC from 0 to 1 and 60 BTC from 1 to 0.
        txs = create_transactions(self.nodes[0], 'TPC', self.nodes[1].getnewaddress(), 40, [Decimal('0.01')])
        self.nodes[0].sendrawtransaction(txs[0]['hex'])
        self.nodes[1].sendrawtransaction(txs[0]['hex'])  # sending on both nodes is faster than waiting for propagation
        self.sync_all()

        txs = create_transactions(self.nodes[1], 'TPC', self.nodes[0].getnewaddress(), 60, [Decimal('0.01'), Decimal('0.04')])
        self.nodes[1].sendrawtransaction(txs[0]['hex'])
        self.nodes[0].sendrawtransaction(txs[0]['hex'])
        self.sync_all()

        self.log.info("Test getbalance and getunconfirmedbalance with unconfirmed inputs")

        # getbalance without any arguments includes unconfirmed transactions, but not untrusted transactions
        assert_equal(round(self.nodes[0].getbalance(), 2), Decimal('9.99'))  # change from node 0's send
        assert_equal(round(self.nodes[1].getbalance(), 2), Decimal('29.99'))  # change from node 1's send

        # getunconfirmedbalance
        assert_equal(self.nodes[0].getunconfirmedbalance(), Decimal('60'))  # output of node 1's spend
        assert_equal(self.nodes[1].getunconfirmedbalance(), Decimal('0'))  # Doesn't include output of node 0's send since it was spent

        # Send 40 colorid1 from 0 to 1 and 60 colorid2 from 1 to 0.
        ctxs = create_transactions(self.nodes[0], colorid1, self.nodes[1].getnewaddress("", colorid1), 40, [Decimal('0.01')])
        self.nodes[0].sendrawtransaction(ctxs[0]['hex'])
        self.nodes[1].sendrawtransaction(ctxs[0]['hex'])
        self.sync_all()

        ctxs = create_transactions(self.nodes[1], colorid2, self.nodes[0].getnewaddress("", colorid2), 60, [Decimal('0.01')])
        self.nodes[1].sendrawtransaction(ctxs[0]['hex'])
        self.nodes[0].sendrawtransaction(ctxs[0]['hex'])
        self.sync_all()

        # getbalance without any arguments includes unconfirmed transactions, but not untrusted transactions
        assert_equal(round(self.nodes[0].getbalance(), 2), Decimal('9.98'))
        assert_equal(round(self.nodes[1].getbalance(), 2), Decimal('29.98'))
        wallet_bal = self.nodes[0].getwalletinfo()['balance']
        assert_equal(wallet_bal[colorid1], 60)
        assert_equal(len(wallet_bal), 2)
        wallet_bal = self.nodes[1].getwalletinfo()['balance']
        assert_equal(wallet_bal[colorid2], 40)
        assert_equal(len(wallet_bal), 2)

        # getunconfirmedbalance
        assert_equal(self.nodes[0].getunconfirmedbalance(), Decimal('60'))
        assert_equal(self.nodes[1].getunconfirmedbalance(), Decimal('0'))
        wallet_bal = self.nodes[0].getwalletinfo()['unconfirmed_balance']
        assert_equal(len(wallet_bal), 1)
        wallet_bal = self.nodes[1].getwalletinfo()['unconfirmed_balance']
        assert_equal(len(wallet_bal), 1)

        # Node 1 bumps the transaction fee and resends
        self.nodes[1].sendrawtransaction(txs[1]['hex'])
        self.sync_all()

        self.log.info("Test getbalance and getunconfirmedbalance with conflicted unconfirmed inputs")

        assert_equal(self.nodes[0].getwalletinfo()["unconfirmed_balance"]['TPC'], Decimal('60'))  # output of node 1's send
        assert_equal(self.nodes[0].getunconfirmedbalance(), Decimal('60'))
        assert_equal(self.nodes[1].getwalletinfo()["unconfirmed_balance"]['TPC'], Decimal('0'))  # Doesn't include output of node 0's send since it was spent
        assert_equal(self.nodes[1].getunconfirmedbalance(), Decimal('0'))

        self.nodes[1].generatetoaddress(1, RANDOM_COINBASE_ADDRESS, self.signblockprivkey_wif)
        self.sync_all()

        # balances are correct after the transactions are confirmed
        assert_equal(round(self.nodes[0].getbalance(), 2), Decimal('69.98'))  # node 1's send plus change from node 0's send
        assert_equal(round(self.nodes[1].getbalance(), 2), Decimal('29.96'))  # change from node 0's send

        wallet_bal = self.nodes[0].getwalletinfo()['balance']
        assert_equal(wallet_bal[colorid1], 60)
        wallet_bal = self.nodes[1].getwalletinfo()['balance']
        assert_equal(wallet_bal[colorid2], 100)

        # Send total balance away from node 1
        txs = create_transactions(self.nodes[1], 'TPC', self.nodes[0].getnewaddress(), Decimal('29.94995520'), [Decimal('0.01')])
        self.nodes[1].sendrawtransaction(txs[0]['hex'])
        self.nodes[1].generatetoaddress(2, RANDOM_COINBASE_ADDRESS, self.signblockprivkey_wif)
        self.sync_all()

        assert_equal(self.nodes[1].getbalance(), Decimal('0'))

if __name__ == '__main__':
    WalletTest().main()
