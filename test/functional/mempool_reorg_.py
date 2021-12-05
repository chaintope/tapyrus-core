#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool re-org scenarios.

Test re-org scenarios with a mempool that contains transactions
that spend (directly or indirectly) coinbase transactions.
"""
import time
from test_framework.blocktools import create_raw_transaction, create_colored_transaction
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class MempoolCoinbaseTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2

    alert_filename = None  # Set by setup_network

    def run_test(self):
        # Start with a 100 block chain
        assert_equal(self.nodes[0].getblockcount(), 100)

        # create a coloured coin transaction 
        res = create_colored_transaction(2, 1000, self.nodes[0])
        colorid = res['color']
        ctxid = res['txid']

        # Mine four blocks. After this, nodes[0] blocks
        # 101, 102, and 103 are spend-able.
        new_blocks = self.nodes[1].generate(1, self.signblockprivkey_wif)
        self.sync_all()

        #create multiple coiored coin outputs for use later
        txid_fee = self.nodes[0].getblock(self.nodes[0].getblockhash(1))['tx'][0]
        color_tx = self.nodes[0].createrawtransaction([{"txid": ctxid, "vout": 0}, {"txid": txid_fee, "vout": 0} ], [{self.nodes[0].getnewaddress("", colorid): 100}, {self.nodes[0].getnewaddress("", colorid): 100}, {self.nodes[0].getnewaddress("", colorid): 100}, {self.nodes[0].getnewaddress("", colorid): 100}, {self.nodes[1].getnewaddress("", colorid): 600}, {self.nodes[0].getnewaddress():49.99}])
        color_txid = self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransactionwithwallet(color_tx, [], "ALL", self.options.scheme)["hex"])

        new_blocks = self.nodes[1].generate(3, self.signblockprivkey_wif)
        self.sync_all()

        node0_address = self.nodes[0].getnewaddress()
        node1_address = self.nodes[1].getnewaddress()
        node0_caddress = self.nodes[0].getnewaddress("", colorid)
        node1_caddress = self.nodes[1].getnewaddress("", colorid)

        for (address_n0, address_n1, amt, height) in [(node0_address, node1_address, 49.99, 2),(node0_caddress, node1_caddress, 50, 8)]:
            # Three scenarios for re-orging coinbase spends in the memory pool:
            # 1. Direct coinbase spend  :  spend_101
            # 2. Indirect (coinbase spend in chain, child in mempool) : spend_102 and spend_102_1
            # 3. Indirect (coinbase and child both in chain) : spend_103 and spend_103_1
            # Use invalidatblock to make all of the above coinbase spends invalid (immature coinbase),
            # and make sure the mempool code behaves correctly.
            self.log.info("From address [%s]" % address_n0)

            #collect coinbase tx ids whose outputs will be spent or used as fee later
            b = [ self.nodes[0].getblockhash(n) for n in range(height, height + 4) ]
            coinbase_txids = [ self.nodes[0].getblock(h)['tx'][0] for h in b ]

            #create transactions with colored coin and with TPC
            if amt == 50:
                spend_101_raw = self.nodes[0].createrawtransaction([{"txid": color_txid, "vout": 1}, {"txid": coinbase_txids[0], "vout": 0}], [{address_n1: amt}, {node0_address: 49.99}])
                spend_101_raw = self.nodes[0].signrawtransactionwithwallet(spend_101_raw, [], "ALL", self.options.scheme)["hex"]

                spend_102_raw = self.nodes[0].createrawtransaction([{"txid": color_txid, "vout": 2}, {"txid": coinbase_txids[1], "vout": 0}], [{address_n0: amt}, {node0_address: 49.99}])
                spend_102_raw = self.nodes[0].signrawtransactionwithwallet(spend_102_raw, [], "ALL", self.options.scheme)["hex"]

                spend_103_raw = self.nodes[0].createrawtransaction([{"txid": color_txid, "vout": 3}, {"txid": coinbase_txids[2], "vout": 0}], [{address_n0: amt}, {node0_address: 49.99}])
                spend_103_raw = self.nodes[0].signrawtransactionwithwallet(spend_103_raw, [], "ALL", self.options.scheme)["hex"]

                # Create a transaction which is time-locked to two blocks in the future
                timelock_tx = self.nodes[0].createrawtransaction([{"txid": color_txid, "vout": 0}, {"txid": coinbase_txids[3], "vout": 0}], [{address_n0: amt}, {node0_address: 49.99}])
            else:
                spend_101_raw = create_raw_transaction(self.nodes[0], coinbase_txids[1], address_n1, amount=amt)
                spend_102_raw = create_raw_transaction(self.nodes[0], coinbase_txids[2], address_n0, amount=amt)
                spend_103_raw = create_raw_transaction(self.nodes[0], coinbase_txids[3], address_n0, amount=amt)
                # Create a transaction which is time-locked to two blocks in the future
                timelock_tx = self.nodes[0].createrawtransaction([{"txid": coinbase_txids[0], "vout": 0}], {address_n0: amt})

                timelock_tx = self.nodes[0].createrawtransaction([{"txid": coinbase_txids[0], "vout": 0}], {address_n0: amt})

            # Set the time lock
            timelock_tx = timelock_tx.replace("ffffffff", "11111191", 1)
            timelock_tx = timelock_tx[:-8] + hex(self.nodes[0].getblockcount() + 2)[2:] + "000000"
            timelock_tx = self.nodes[0].signrawtransactionwithwallet(timelock_tx, [], "ALL", self.options.scheme)["hex"]
            # This will raise an exception because the timelock transaction is too immature to spend
            assert_raises_rpc_error(-26, "non-final", self.nodes[0].sendrawtransaction, timelock_tx)

            # Broadcast and mine spend_102 and 103:
            spend_102_id = self.nodes[0].sendrawtransaction(spend_102_raw)
            spend_103_id = self.nodes[0].sendrawtransaction(spend_103_raw)
            self.nodes[0].generate(1, self.signblockprivkey_wif)
            # Time-locked transaction is still too immature to spend
            assert_raises_rpc_error(-26, 'non-final', self.nodes[0].sendrawtransaction, timelock_tx)

            # Create 102_1 and 103_1:
            if amt == 50:
                spend_102_1_raw = self.nodes[0].createrawtransaction([{"txid": spend_102_id, "vout": 0}, {"txid": spend_102_id, "vout": 1}], [{address_n1: amt}, {node0_address: 49.98}])
                spend_102_1_raw = self.nodes[0].signrawtransactionwithwallet(spend_102_1_raw, [], "ALL", self.options.scheme)["hex"]

                spend_103_1_raw = self.nodes[0].createrawtransaction([{"txid": spend_103_id, "vout": 0}, {"txid": spend_103_id, "vout": 1}], [{address_n1: amt}, {node0_address: 49.98}])
                spend_103_1_raw = self.nodes[0].signrawtransactionwithwallet(spend_103_1_raw, [], "ALL", self.options.scheme)["hex"]
            else:
                spend_102_1_raw = create_raw_transaction(self.nodes[0], spend_102_id, address_n1, amount=49.98)
                spend_103_1_raw = create_raw_transaction(self.nodes[0], spend_103_id, address_n1, amount=49.98)

            # Broadcast and mine 103_1:
            spend_103_1_id = self.nodes[0].sendrawtransaction(spend_103_1_raw)
            last_block = self.nodes[0].generate(1, self.signblockprivkey_wif)
            self.log.info("spend_103_1_id %s" % spend_103_1_id)
            # Time-locked transaction can now be spent
            timelock_tx_id = self.nodes[0].sendrawtransaction(timelock_tx)

            # ... now put spend_101 and spend_102_1 in memory pools:
            spend_101_id = self.nodes[0].sendrawtransaction(spend_101_raw)
            spend_102_1_id = self.nodes[0].sendrawtransaction(spend_102_1_raw)

            self.sync_all()

            assert_equal(set(self.nodes[0].getrawmempool()), {spend_101_id, spend_102_1_id, timelock_tx_id})

            for node in self.nodes:
                node.invalidateblock(last_block[0])
            # Time-locked transaction is now too immature and has been removed from the mempool
            # spend_103_1 has been re-orged out of the chain and is back in the mempool
            assert_equal(set(self.nodes[0].getrawmempool()), {spend_101_id, spend_102_1_id, spend_103_1_id})

            # Use invalidateblock to re-org back and make all those coinbase spends
            # immature/invalid:
            for node in self.nodes:
                node.invalidateblock(new_blocks[0])

            self.sync_all()

            # mempool should be empty.
            assert_equal(set(self.nodes[0].getrawmempool()), {spend_101_id, spend_102_id, spend_102_1_id, spend_103_id, spend_103_1_id, ctxid, color_txid})

            self.log.info("Done")

if __name__ == '__main__':
    MempoolCoinbaseTest().main()
