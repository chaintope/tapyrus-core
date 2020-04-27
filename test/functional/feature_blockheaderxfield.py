#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""xFieldType and xField in block header.

This test checks that the node can handle new blocks with known and unknown xFieldType and xfield.

"""

import struct
from io import BytesIO

from test_framework.schnorr import Schnorr
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.mininode import P2PInterface, P2PDataStore
from test_framework.messages import (
    CBlock,
    CBlockHeader,
    bytes_to_hex_str,
    hex_str_to_bytes,
    ser_uint256,
    ser_string,
    ser_vector,
    hash256
)

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error
)
from test_framework.blocktools import create_block, create_coinbase, create_transaction

class BlockHeaderXFieldTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-whitelist=127.0.0.1"]]
        self.signKey = Schnorr()
        self.signKey.set_secretbytes(hex_str_to_bytes(self.signblockprivkey))

    def createblockproof(self, block, signblockprivkey):
        # create block proof with xField for all xfieldTypes
        r = b""
        r += struct.pack("<i", block.nVersion)
        r += ser_uint256(block.hashPrevBlock)
        r += ser_uint256(block.hashMerkleRoot)
        r += ser_uint256(block.hashImMerkleRoot)
        r += struct.pack("<I", block.nTime)
        r += struct.pack("B", block.xfieldType)
        r += ser_string(block.xfield)
        sighash = hash256(r)
        block.proof = bytearray(self.signKey.sign(sighash))
        block.rehash()

    def serializeblock_err(self, block):
        r = b""
        r += struct.pack("<i", block.nVersion)
        r += ser_uint256(block.hashPrevBlock)
        r += ser_uint256(block.hashMerkleRoot)
        r += ser_uint256(block.hashImMerkleRoot)
        r += struct.pack("<I", block.nTime)
        r += struct.pack("B", block.xfieldType)
        r += block.xfield
        r += ser_string(block.proof)
        r += ser_vector(block.vtx)
        return r

    def reconnect_p2p(self):
        """Tear down and bootstrap the P2P connection to the node.

        The node gets disconnected several times in this test. This helper
        method reconnects the p2p and restarts the network thread."""
        self.nodes[0].disconnect_p2ps()
        self.nodes[0].add_p2p_connection(P2PDataStore())
        self.nodes[0].p2p.wait_for_getheaders(timeout=5)

    def run_test(self):
        node = self.nodes[0]
        node.add_p2p_connection(P2PDataStore())
        node.p2p.wait_for_getheaders(timeout=5)

        self.tip = node.getbestblockhash()
        best_block = node.getblock(self.tip)
        block_time = best_block["time"] + 1

        self.log.info("Incorrect xfield with xfieldType = 0")
        block = create_block(int(self.tip, 16), create_coinbase(1), block_time)
        block.xfieldType = 0
        block.xfield = b'1' 
        self.createblockproof(block, self.signblockprivkey)

        node.p2p.send_blocks_and_test([block], node, success=False, request_block=False)

        self.log.info("Incorrect xfield with xfieldType = 1")
        block = create_block(int(self.tip, 16), create_coinbase(1), block_time)
        block.xfieldType = 1
        block.xfield = b'1' 
        self.createblockproof(block, self.signblockprivkey)

        node.p2p.send_blocks_and_test([block], node, success=False, request_block=False, reject_code=16, reject_reason="bad-xfieldType-xfield")

        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo['warnings'], "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications")

        self.log.info("Incorrect xfield with xfieldType = 2")
        block = create_block(int(self.tip, 16), create_coinbase(1), block_time)
        block.xfieldType = 2
        block.xfield = b'1'
        self.createblockproof(block, self.signblockprivkey)

        assert_raises_rpc_error(-22, "", node.submitblock, bytes_to_hex_str(self.serializeblock_err(block)))

        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo['warnings'], "This is a pre-release test build - use at your own risk - do not use for mining or merchant applications")

        self.log.info("Valid xfield with xfieldType = 2")
        node.p2p.send_blocks_and_test([block], node, True, True)
        assert_equal(block.hash, node.getbestblockhash())

        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo['warnings'], "Warning: Unknown xfieldType [%2x] was accepted in block [%s]" % (block.xfieldType, block.hash))

        self.log.info("Valid xfield with xfieldType = 0xFF")
        self.tip = node.getbestblockhash()
        block_time += 1
        block = create_block(int(self.tip, 16), create_coinbase(2), block_time)
        block.xfieldType = 0xFF
        block.xfield = b'1'
        self.createblockproof(block, self.signblockprivkey)

        node.p2p.send_blocks_and_test([block], node, True, True)
        assert_equal(block.hash, node.getbestblockhash())

        self.log.info("xfieldType = 3 in invalid block - insufficient tx input")
        self.tip = node.getbestblockhash()
        block_time += 1
        block = create_block(int(self.tip, 16), create_coinbase(3), block_time)
        block.xfieldType = 3
        block.xfield = b''
        spendHash = node.getblock(self.tip)['tx'][0]
        block.vtx += [create_transaction(node, spendHash, node.getnewaddress(), amount=100.0)]
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        self.createblockproof(block, self.signblockprivkey)
        oldblockhash = block.hash

        node.p2p.send_blocks_and_test([block], node, success=False, request_block=True, reject_code=16, reject_reason="bad-txns-in-belowout")
        assert_equal(self.tip, node.getbestblockhash())

        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo['warnings'], "Warning: Unknown xfieldType [%2x] was accepted in block [%s]" % (block.xfieldType, block.hash))

        self.log.info("xfieldType = 4 in invalid block - height in coinbase")
        block_time += 1
        block = create_block(int(self.tip, 16), create_coinbase(1), block_time)
        block.xfieldType = 4
        block.xfield = b''
        self.createblockproof(block, self.signblockprivkey)

        node.p2p.send_blocks_and_test([block], node, success=False, request_block=True, reject_code=16, reject_reason="bad-cb-invalid")
        assert_equal(self.tip, node.getbestblockhash())

        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo['warnings'], "Warning: Unknown xfieldType [%2x] was accepted in block [%s]" % (3, oldblockhash))

        self.log.info("xfieldType = 5 in invalid block - no coinbase")
        block_time += 1
        block = create_block(int(self.tip, 16), create_coinbase(4), block_time)
        block.xfieldType = 5
        block.xfield = b''
        block.vtx = []
        self.createblockproof(block, self.signblockprivkey)

        node.p2p.send_blocks_and_test([block], node, success=False, request_block=True, reject_code=16, reject_reason="bad-cb-missing")
        assert_equal(self.tip, node.getbestblockhash())

        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo['warnings'], "Warning: Unknown xfieldType [%2x] was accepted in block [%s]" % (3, oldblockhash))

if __name__ == '__main__':
    BlockHeaderXFieldTest().main()
