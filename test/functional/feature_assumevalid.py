#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test logic for skipping signature validation on old blocks.

Test logic for skipping signature validation on blocks which we've assumed
valid (https://github.com/bitcoin/bitcoin/pull/9484)

We build a chain that includes and invalid signature for one of the
transactions:

    0:        genesis block
    1:        block 1 with coinbase transaction output.
    2:      a block containing a transaction spending the coinbase
              transaction output. The transaction has an invalid signature.
    3-202: bury the bad block with 200 blocks

Start 2 nodes:

    - node0 has no -assumevalid parameter. Try to sync to block 202. It will
      reject block 2 and only sync as far as block 1
    - node1 has -assumevalid set to the hash of block 2. Try to sync to
      block 202. node1 will sync all the way to block 202.
"""
import time

from test_framework.blocktools import (create_block, create_coinbase)
from test_framework.key import CECKey
from test_framework.messages import (
    CBlockHeader,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    msg_block,
    msg_headers
)
from test_framework.mininode import P2PInterface
from test_framework.script import (CScript, OP_TRUE)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

class BaseNode(P2PInterface):
    def send_header_for_blocks(self, new_blocks):
        headers_message = msg_headers()
        headers_message.headers = [CBlockHeader(b) for b in new_blocks]
        self.send_message(headers_message)

class AssumeValidTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def setup_network(self):
        self.add_nodes(2)
        # Start node0. We don't start the other nodes yet since
        # we need to pre-mine a block with an invalid transaction
        # signature so we can pass in the block hash as assumevalid.
        self.start_node(0)

    def send_blocks_until_disconnected(self, p2p_conn):
        """Keep sending blocks to the node until we're disconnected."""
        for i in range(len(self.blocks)):
            if not p2p_conn.is_connected:
                break
            try:
                p2p_conn.send_message(msg_block(self.blocks[i]))
            except IOError as e:
                assert not p2p_conn.is_connected
                break

    def assert_blockchain_height(self, node, height):
        """Wait until the blockchain is no longer advancing and verify it's reached the expected height."""
        last_height = node.getblock(node.getbestblockhash())['height']
        timeout = 10
        while True:
            time.sleep(0.25)
            current_height = node.getblock(node.getbestblockhash())['height']
            if current_height != last_height:
                last_height = current_height
                if timeout < 0:
                    assert False, "blockchain too short after timeout: %d" % current_height
                timeout - 0.25
                continue
            elif current_height > height:
                assert False, "blockchain too long: %d" % current_height
            elif current_height == height:
                break
            else:
                assert False, "blockchain not progressing: %d" % current_height

    def run_test(self):
        p2p0 = self.nodes[0].add_p2p_connection(BaseNode(self.nodes[0].time_to_connect))
        self.stop_node(1)

        # Build the blockchain
        self.tip = int(self.nodes[0].getbestblockhash(), 16)
        self.block_time = self.nodes[0].getblock(self.nodes[0].getbestblockhash())['time'] + 1

        self.blocks = []

        # Get a pubkey for the coinbase TXO
        coinbase_key = CECKey()
        coinbase_key.set_secretbytes(b"horsebattery")
        coinbase_pubkey = coinbase_key.get_pubkey()

        # Create the first block with a coinbase output to our key
        height = 1
        block = create_block(self.tip, create_coinbase(height, coinbase_pubkey), self.block_time)
        self.block_time += 1
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.solve(self.signblockprivkey)
        # Save the coinbase for later
        self.blocks.append(block)
        self.block1 = block
        self.tip = block.sha256
        height += 1

        # Create a transaction spending the coinbase output with an invalid (null) signature
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(self.block1.vtx[0].malfixsha256, 0), scriptSig=b""))
        tx.vout.append(CTxOut(49 * 100000000, CScript([OP_TRUE])))
        tx.calc_sha256()

        block2 = create_block(self.tip, create_coinbase(height), self.block_time)
        self.block_time += 1
        block2.vtx.extend([tx])
        block2.hashMerkleRoot = block2.calc_merkle_root()
        block2.hashImMerkleRoot = block2.calc_immutable_merkle_root()
        block2.solve(self.signblockprivkey)
        block2.rehash()
        self.blocks.append(block2)
        self.tip = block2.sha256
        self.block_time += 1
        height += 1

        # Bury the assumed valid block 200 deep
        for i in range(200):
            block = create_block(self.tip, create_coinbase(height), self.block_time)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.hashImMerkleRoot = block.calc_immutable_merkle_root()
            block.solve(self.signblockprivkey)
            self.blocks.append(block)
            self.tip = block.sha256
            self.block_time += 1
            height += 1

        self.nodes[0].disconnect_p2ps()

        # Start node1 and node2 with assumevalid so they accept a block with a bad signature.
        self.start_node(1, extra_args=["-assumevalid=" + hex(block2.sha256)])

        p2p0 = self.nodes[0].add_p2p_connection(BaseNode(self.nodes[0].time_to_connect))
        p2p1  = self.nodes[1].add_p2p_connection(BaseNode(self.nodes[1].time_to_connect))

        # send header lists to all three nodes
        p2p0.send_header_for_blocks(self.blocks[0:200])
        p2p0.send_header_for_blocks(self.blocks[200:])
        p2p1.send_header_for_blocks(self.blocks[0:200])
        p2p1.send_header_for_blocks(self.blocks[200:])

        # Send blocks to node0. Block 2 will be rejected.
        self.send_blocks_until_disconnected(p2p0)
        self.assert_blockchain_height(self.nodes[0], 1)

        # Send all blocks to node1. All blocks will be accepted.
        for i in range(202):
            p2p1.send_message(msg_block(self.blocks[i]))
            if i < 2:
                time.sleep(5)
        assert_equal(self.nodes[1].getblock(self.nodes[1].getbestblockhash())['height'], 202)


if __name__ == '__main__':
    AssumeValidTest().main()
