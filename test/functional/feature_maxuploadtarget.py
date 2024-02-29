#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test behavior of -maxuploadtarget.

* Verify that getdata requests for old blocks (>1week) are dropped
if uploadtarget has been reached.
* Verify that getdata requests for recent blocks are respected even
if uploadtarget has been reached.
* Verify that the upload counters are reset after 24 hours.
"""
from collections import defaultdict
import time

from test_framework.blocktools import createTestGenesisBlock
from test_framework.messages import CInv, msg_getdata, ToHex
from test_framework.mininode import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes, connect_nodes_bi
from test_framework.script import CScript
from test_framework.messages import CTransaction, CTxOut, CTxIn, COutPoint, COIN, MSG_TX, MSG_TYPE_MASK, MSG_BLOCK, msg_block, msg_tx

MAX_SCRIPT_SIZE = 10000
MAX_SCRIPT_ELEMENT_SIZE = 520

class TestP2PConn(P2PInterface):
    def __init__(self, time_to_connect):
        super().__init__(time_to_connect)
        self.block_receive_map = defaultdict(int)

    def on_inv(self, message):
        pass

    def on_block(self, message):
        message.block.calc_sha256()
        self.block_receive_map[message.block.sha256] += 1

class MaxUploadTest(BitcoinTestFramework):

    def mine_large_block(self, node, utxos):
        self.send_txs_for_large_block(node, utxos)
        return node.generate(1, self.signblockprivkey_wif)[0]

    def send_txs_for_large_block(self, node, utxos, size=1000000):
        current_size = 0
        i = 0
        while size - current_size > MAX_SCRIPT_SIZE and i < len(utxos):
            spend_addr = node.gettransaction(utxos[i]['txid'])['details'][0]['address']
            scr = node.getaddressinfo(spend_addr)['scriptPubKey']
            tx = self.create_tx_with_large_script(int(utxos[i]['txid'], 16), 0, CScript(hex_str_to_bytes(scr)))
            tx_raw = ToHex(tx)
            current_size = current_size + len(tx_raw)
            tx_signed = node.signrawtransactionwithwallet(tx_raw)
            status = node.sendrawtransaction(tx_signed['hex'], True)
            i = i + 1

    def create_tx_with_large_script(self, prevtx, n, scriptPubKey):
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(prevtx, n), b"", 0xffffffff))
        tx.vout.append(CTxOut(48*COIN, scriptPubKey))
        current_size = 0
        script_output = CScript([b''])
        while  MAX_SCRIPT_SIZE - current_size  > MAX_SCRIPT_ELEMENT_SIZE:
            script_output = script_output + CScript([b'\x6a', b'\x51' * (MAX_SCRIPT_ELEMENT_SIZE - 5) ])
            current_size = current_size + MAX_SCRIPT_ELEMENT_SIZE
        tx.vout.append(CTxOut(1*COIN, script_output))
        tx.calc_sha256()
        return tx

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        # Before we connect anything, we first set the time on the node
        # to be in the past, otherwise things break because the CNode
        # time counters can't be reset backward after initialization
        self.mocktime =  int(time.time() - 2*60*60*24*7)
        self.genesisBlock =  createTestGenesisBlock(self.signblockpubkey, self.signblockprivkey, self.mocktime)

        self.utxo_cache = []

    def setup_network(self):
        self.add_nodes(3)
        self.start_node(0, ["-maxuploadtarget=200"])
        self.start_node(1)
        self.start_node(2, ["-maxuploadtarget=800"])
        connect_nodes_bi(self.nodes, 0,1)
        connect_nodes_bi(self.nodes, 0,2)

    def run_test(self):
        # Generate some old blocks
        self.nodes[0].generate(31, self.signblockprivkey_wif)
        self.utxo_cache = self.nodes[0].listunspent()
        self.sync_all()

        # p2p_conns[0] will only request old blocks and resetting the counters
        # p2p_conns[1] will request new and old blocks
        # p2p_conns[2] will test whitelist
        p2p_conns = []

        p2p_conns.append(self.nodes[0].add_p2p_connection(TestP2PConn(self.nodes[0].time_to_connect)))
        p2p_conns.append(self.nodes[1].add_p2p_connection(TestP2PConn(self.nodes[1].time_to_connect)))
        p2p_conns.append(self.nodes[2].add_p2p_connection(TestP2PConn(self.nodes[2].time_to_connect)))

        # Now mine a big block
        big_old_block = self.mine_large_block(self.nodes[0], self.utxo_cache)
        self.sync_all()

        # Store the hash; we'll request this later
        old_block_size = self.nodes[0].getblock(big_old_block, True)['size']
        big_old_block = int(big_old_block, 16)

        self.nodes[0].generate(100, self.signblockprivkey_wif)
        self.sync_all()

        # Advance to two days ago
        self.mocktime =  int(time.time()) - 2*60*60*24
        self.nodes[0].setmocktime(self.mocktime)
        self.nodes[1].setmocktime(self.mocktime)
        self.nodes[2].setmocktime(self.mocktime)

        self.nodes[0].generate(31, self.signblockprivkey_wif)
        self.sync_all()

        # Mine one more block, so that the prior block looks old
        self.utxo_cache = [i for i in  self.nodes[0].listunspent() if i['amount'] > 49 * COIN]
        self.mine_large_block(self.nodes[0], self.utxo_cache)

        # We'll be requesting this new block too
        big_new_block = self.nodes[0].getbestblockhash()
        new_block_size = self.nodes[0].getblock(big_new_block, True)['size']
        big_new_block = int(big_new_block, 16)

        # p2p_conns[0] will test what happens if we just keep requesting the
        # the same big old block too many times (expect: disconnect)

        getdata_request = msg_getdata()
        getdata_request.inv.append(CInv(2, big_old_block))

        # test send/recv limit
        limits = self.nodes[0].getnettotals()
        for i in range(210):
            p2p_conns[0].send_message(getdata_request)
            p2p_conns[0].sync_with_ping()
        p2p_conns[0].send_message(getdata_request)
        p2p_conns[0].wait_for_disconnect()
        assert(limits['uploadtarget']['bytes_left_in_cycle'] > self.nodes[0].getnettotals()['uploadtarget']['bytes_left_in_cycle'] + 210 * old_block_size)
        assert_equal(p2p_conns[0].block_receive_map[big_old_block], 210)
        self.log.info("Peer 0 disconnected after downloading old block too many times")

        # Requesting the current block on p2p_conns[1] should succeed indefinitely,
        # even when over the max upload target.
        # We'll try 800 times
        getdata_request.inv = [CInv(2, big_new_block)]
        limits = self.nodes[1].getnettotals()
        for i in range(800):
            p2p_conns[1].send_message(getdata_request)
            p2p_conns[1].sync_with_ping()
        assert_equal(p2p_conns[1].block_receive_map[big_new_block], 800)
        self.log.info("Peer 1 able to repeatedly download new block")

        # p2p_conns[1] tries for old block .
        getdata_request.inv = [CInv(2, big_old_block)]
        limits = self.nodes[1].getnettotals()
        for i in range (800):
            p2p_conns[1].send_message(getdata_request)
            p2p_conns[1].sync_with_ping()
        assert_equal(p2p_conns[1].block_receive_map[big_old_block], 800)
        self.log.info("Advancing system time on node to clear counters...")

        # If we advance the time by 24 hours, then the counters should reset,
        # and p2p_conns[0] should be able to retrieve the old block.
        self.mocktime =  int(time.time())
        self.nodes[0].setmocktime(self.mocktime)
        self.nodes[1].setmocktime(self.mocktime)
        self.nodes[2].setmocktime(self.mocktime)
        p2p_conns[0] = self.nodes[0].add_p2p_connection(TestP2PConn(self.nodes[0].time_to_connect))
        p2p_conns[0].send_message(getdata_request)
        p2p_conns[0].sync_with_ping()
        self.log.info("Peer 0 able to download old block")

        self.log.info("Peer 2 with -whitelist=127.0.0.1")
        self.restart_node(2, ["-whitelist=127.0.0.1", "-reindex", "-maxuploadtarget=1"])
        p2p_conns[2]  = self.nodes[2].add_p2p_connection(TestP2PConn(self.nodes[2].time_to_connect))

        #retrieve 20 blocks which should be enough to break the 1MB limit
        getdata_request.inv = [CInv(2, big_new_block)]
        for i in range(20):
            p2p_conns[2].send_message(getdata_request)
            p2p_conns[2].sync_with_ping()

        getdata_request.inv = [CInv(2, big_old_block)]
        self.nodes[2].p2p.send_and_ping(getdata_request)

if __name__ == '__main__':
    MaxUploadTest().main()
