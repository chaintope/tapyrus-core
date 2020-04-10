#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test node responses to federation blocks.

In this test we connect to one node over p2p, and test block requests:
1) Block create with new aggregate public key sign with old aggregate public key
2) The Next block will sign with the new aggregate public key
"""
import copy

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import P2PDataStore
from test_framework.test_framework import BitcoinTestFramework

class FederationBlockTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):
        # Add p2p connection to node0
        node = self.nodes[0]  # convenience reference to the node
        node.add_p2p_connection(P2PDataStore())

        best_block = node.getblock(node.getbestblockhash())
        tip = int(node.getbestblockhash(), 16)
        # tip = best_block.sha256
        height = best_block["height"] + 1
        block_time = best_block["time"] + 1

        self.log.info("Create 1st block")
        height = 1
        block1 = create_block(tip, create_coinbase(height), block_time, self.signblockpubkey)
        block1.solve(self.signblockprivkey)
        node.p2p.send_blocks_and_test([block1], node, True)

        best_block = node.getblock(node.getbestblockhash())

        tip = block1.sha256
        # tip = int(node.getbestblockhash(), 16)
        height = best_block["height"] + 1
        block_time = best_block["time"] + 1

        self.log.info("Create 2nd block with new aggpubkey")
        self.signblockpubkey = "03842d51608d08bee79587fb3b54ea68f5279e13fac7d72515a7205e6672858ca2"
        block2 = create_block(tip, create_coinbase(height), block_time, self.signblockpubkey)
        block2.solve(self.signblockprivkey)
        node.p2p.send_blocks_and_test([block2], node, True)

        best_block = node.getblock(node.getbestblockhash())
        tip = block2.sha256
        # tip = int(node.getbestblockhash(), 16)
        height = best_block["height"] + 1
        block_time = best_block["time"] + 1

        self.signblockprivkey = "657440783dd10977c49f87c51dc68b63508e88c7ea9371dc19e6fcd0f5f8639e"
        self.log.info("Create 3rd block")
        block3 = create_block(tip, create_coinbase(height), block_time, self.signblockpubkey)
        block3.solve(self.signblockprivkey)
        node.p2p.send_blocks_and_test([block3], node, True)
        best_block = node.getblock(node.getbestblockhash())
        self.log.info(best_block)


if __name__ == '__main__':
    FederationBlockTest().main()
