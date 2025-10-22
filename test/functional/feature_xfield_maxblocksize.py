
#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test max block size change in the blockchain.
xfield type 2 is max block size

Test with different block sizes:
MAX_BLOCK_BASE_SIZE
MAX_BLOCK_BASE_SIZE/2,
3 * MAX_BLOCK_BASE_SIZE
3999500 bytes (as accepts only MAX_PROTOCOL_MESSAGE_LENGTH (4MB) messages in p2p protocol)

Simulate Blockchain Reorg  - After the last federation block and Before the last federation block

Test the output of RPC Getblockchaininfo before reorg and after reorg. Verify block aggpubkey and maxblock size changes against block height

Restart the node with -reindex, -reindex-chainstate, -reloadxfield and -loadblock options. This triggers a full rewind of block index. Verify that the tip reaches B51 at the end.
"""
import time
import shutil, os

from test_framework.blocktools import create_block, create_coinbase, createTestGenesisBlock, generate_blocks
from test_framework.key import CECKey
from test_framework.schnorr import Schnorr
from test_framework.mininode import P2PDataStore
from test_framework.timeout_config import TAPYRUSD_MIN_TIMEOUT, TAPYRUSD_REORG_TIMEOUT
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, bytes_to_hex_str, hex_str_to_bytes, NetworkDirName, connect_nodes, assert_raises_rpc_error, wait_until
from test_framework.script import CScript, OP_CHECKSIG, OP_TRUE, SignatureHash, SIGHASH_ALL, MAX_SCRIPT_SIZE
from test_framework.messages import CTransaction, MAX_BLOCK_BASE_SIZE, CTxOut, CTxIn, COutPoint, uint256_from_str, ser_compact_size, msg_headers, CBlockHeader

TAPYRUSD_SYNC_TIMEOUT = 1000
TAPYRUSD_REORG_TIMEOUT = 1000

MAX_BLOCK_SIGOPS = int((100000 - 1000) / 50)

reverse_bytes = (lambda txid  : txid[-1: -len(txid)-1: -1])

# TestP2PConn: A peer we use to send messages to bitcoind, and store responses.
class TestP2PConn(P2PDataStore):
    def __init__(self, time_to_connect):
        super().__init__(time_to_connect)
        self.last_sendcmpct = []
        self.block_announced = False
        # Store the hashes of blocks we've seen announced.
        # This is for synchronizing the p2p message traffic,
        # so we can eg wait until a particular block is announced.
        self.announced_blockhashes = set()

    def on_sendcmpct(self, message):
        self.last_sendcmpct.append(message)

    def on_inv(self, message):
        for x in self.last_message["inv"].inv:
            if x.type == 2:
                self.block_announced = True
                self.announced_blockhashes.add(x.hash)

    def send_header_for_blocks(self, new_blocks):
        for block in new_blocks:
            self.block_store[block.sha256] = block
        headers_message = msg_headers()
        headers_message.headers = [CBlockHeader(b) for b in new_blocks]
        self.send_message(headers_message)

class MaxBloxkSizeInXFieldTest(BitcoinTestFramework):
    def set_test_params(self):
        self.aggpubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc",
        "02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf",]
        
        self.aggprivkey = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37",
        "dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b",
        "aa2c70c4b85a09be514292d04b27bbb0cc3f86d306d58fe87743d10a095ada07",]

        self.aggprivkey_wif = ["cR4F4fGuKjDWxiYDtGtyM77WkrVhTgokVyM2ERxoxp7R4RQP9dgE",
        "cUwpWhH9CbYwjUWzfz1UVaSjSQm9ALXWRqeFFiZKnn8cV6wqNXQA",
        "cTHVmjaAwKtU75t89fg42SLx43nRxhsri6YY1Eynvs1V1tPRCfae",]

        self.blocks = []

        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()

        self.schnorr_key = Schnorr()
        self.schnorr_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))

        #test has 4 nodes.
        # node0 is the test node where all blocks are sent.
        # node1 is passive, always in synch with node0
        # node2 is started and stopped at different points to test network synch
        # node3 is a new node added to the network at the end of the test
        self.num_nodes = 4
        self.sig_scheme = 0
        self.setup_clean_chain = True
        self.mocktime = int(time.time() - 50)
        self.genesisBlock = createTestGenesisBlock(self.aggpubkeys[0], self.aggprivkey[0], int(time.time() - 100))

    def reconnect_p2p(self, node):
        node.disconnect_p2ps()
        node.add_p2p_connection(TestP2PConn(self.nodes[0].time_to_connect))
        node.p2p.wait_for_getheaders(timeout=TAPYRUSD_MIN_TIMEOUT)

    def run_test(self):
        node = self.nodes[0]
        syn_node = self.nodes[1]
        self.address = node.getnewaddress()
        self.reconnect_p2p(node)
        self.stop_node(2)
        self.stop_node(3)

        self.log.info("Test starting...")

        #genesis block (B0)
        self.blocks = [node.getblock(self.genesisBlock.hash) ]
        self.block_time = int(time.time())

        # Create new blocks B1 - B10
        self.unspent = generate_blocks(10, node, self.coinbase_pubkey, self.aggprivkey[0])
        self.tip  = node.getbestblockhash()

        #B11 -  Create block - small block size - sign with aggpubkey1 -- success
        self.block_time += 1
        blocknew = self.new_block(11, spend=self.unspent[0], coinbase_pubkey=self.coinbase_pubkey)
        blocknew.solve(self.aggprivkey[0])
        self.expand_block(blocknew, 20000)
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B12 -  Create block - block size 1MB - sign with aggpubkey1 -- success
        self.block_time += 1
        blocknew = self.new_block(12, spend=self.unspent[1])
        blocknew.solve(self.aggprivkey[0])
        self.expand_block(blocknew, MAX_BLOCK_BASE_SIZE - 1000)
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        self.sync_all([self.nodes[0:2]])
        assert_equal(self.tip, syn_node.getbestblockhash())
        assert(node.getblock(self.tip))

        self.log.info("Reject a block of size MAX_BLOCK_BASE_SIZE + 1")
        #B -  Create block - block size 1MB +1 - sign with aggpubkey1 -- failure
        self.block_time += 1
        blocknew = self.new_block(13, spend=self.unspent[2])
        blocknew.solve(self.aggprivkey[0])
        self.expand_block(blocknew, MAX_BLOCK_BASE_SIZE + 1)
        node.p2p.send_blocks_and_test([blocknew], node, success=False, reject_code=16, reject_reason="bad-blk-length")
        assert_equal(self.tip, node.getbestblockhash())

        self.reconnect_p2p(node)
        chaintips = node.getchaintips()
        assert_equal(len(chaintips), 2)

        #B13 -  Create block - block MAX_BLOCK_SIGOPS - sign with aggpubkey1 -- success
        self.log.info("Accept a block with MAX_BLOCK_SIGOPS checksigs")
        self.block_time += 1
        blocknew = self.new_block(13, spend=self.unspent[2], script=CScript([OP_CHECKSIG] * (MAX_BLOCK_SIGOPS - 1)))
        blocknew.solve(self.aggprivkey[0])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B -  Create block - block more than MAX_BLOCK_SIGOPS - sign with aggpubkey1 -- failure
        self.log.info("Reject a block with too many checksigs")
        self.block_time += 1
        blocknew = self.new_block(14, spend=self.unspent[3], script=CScript([OP_CHECKSIG] * 20001))#MAX_BLOCK_SIGOPS
        blocknew.solve(self.aggprivkey[0])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, reject_code=16, reject_reason="bad-blk-sigops")
        assert_equal(self.tip, node.getbestblockhash())

        self.reconnect_p2p(node)
        chaintips = node.getchaintips()
        assert_equal(len(chaintips), 3)

        self.block_time += 1
        blocknew = self.new_block(14, spend=self.unspent[3])
        blocknew.solve(self.aggprivkey[0])
        self.expand_block(blocknew, MAX_BLOCK_BASE_SIZE - 1000)
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        self.block_time += 1
        blocknew = self.new_block(15, spend=self.unspent[4])
        blocknew.solve(self.aggprivkey[0])
        self.expand_block(blocknew, MAX_BLOCK_BASE_SIZE - 1000)
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        self.sync_all([self.nodes[0:2]])
        assert_equal(self.tip, syn_node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B16 -- Create block with aggpubkey2 - sign with aggpubkey1 -- success - aggpubkey2 is added to list 
        self.log.info("Accept block which changes aggpubkey")
        self.block_time += 1
        blocknew = self.new_block(16, spend=self.unspent[5], signblockpubkey=self.aggpubkeys[1])
        blocknew.solve(self.aggprivkey[0])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        b16 = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #restarting node0 to test presistance of aggpubkey change
        self.stop_node(0)
        self.start_node(0)
        connect_nodes(self.nodes[0], 1)
        self.reconnect_p2p(node)

        #B -- Create block with 0 maxblock size - sign with aggpubkey2 -- failure - block serialization fails
        self.log.info("Reject blocks with invalid max block size")
        self.block_time += 1
        blocknew = self.new_block(17, spend=self.unspent[6])
        blocknew.xfieldType = 2
        blocknew.xfield = 0
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(self.tip, node.getbestblockhash())

        #B -- Create block with small maxblock size - sign with aggpubkey2 -- failure - max block size invalid
        self.block_time += 1
        blocknew = self.new_block(17, spend=self.unspent[6])
        blocknew.xfieldType = 2
        blocknew.xfield = 100
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(self.tip, node.getbestblockhash())

        #B -- Create block with negative maxblock size - sign with aggpubkey2 -- failure - max block size invalid
        self.block_time += 1
        blocknew = self.new_block(17, spend=self.unspent[6])
        blocknew.xfieldType = 2
        blocknew.xfield = 0
        blockhex = blocknew.serialize()
        blockhex=blockhex.replace(b'\x02\x00\x00\x00\x00', b'\x02\x80\x80\x80\x80')
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(self.tip, node.getbestblockhash())

        #B17 -- Create block - new max block size 0.5MB - sign with aggpubkey2 -- success
        self.log.info("Accept block which changes max block size to 0.5MB")
        self.reconnect_p2p(node)
        self.block_time += 1
        blocknew = self.new_block(17, spend=self.unspent[7])
        blocknew.xfieldType = 2
        blocknew.xfield = int(MAX_BLOCK_BASE_SIZE/2)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B -- Create block - block size 1MB - sign with aggpubkey2 -- failure - max block size exceeded
        self.block_time += 1
        blocknew = self.new_block(18, spend=self.unspent[8])
        self.expand_block(blocknew, MAX_BLOCK_BASE_SIZE - 1000)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(self.tip, node.getbestblockhash())

        #B18 -- Create block - block size 0.5B -- success
        self.reconnect_p2p(node)
        chaintips = node.getchaintips()
        assert_equal(len(chaintips), 4)

        self.block_time += 1
        blocknew = self.new_block(18, spend=self.unspent[8])
        self.expand_block(blocknew, int(MAX_BLOCK_BASE_SIZE/2))
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B19 - B28 -- Generate 10 blocks - no change in aggpubkey or block size -- chain becomes longer
        self.unspent = self.unspent + generate_blocks(10, node, self.coinbase_pubkey, self.aggprivkey[1])
        self.tip  = node.getbestblockhash()
        assert_equal(self.tip, node.getbestblockhash())

        #B -- Create block - block size 3MB - sign with aggpubkey2 -- failure - max block size exceeded
        self.block_time += 1
        blocknew = self.new_block(29, spend=self.unspent[9])
        self.expand_block(blocknew, 3 * MAX_BLOCK_BASE_SIZE)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(self.tip, node.getbestblockhash())

        #B29 -- Create block - new  maxblocksize 3MB - sign with aggpubkey2 -- success
        self.log.info("Accept block which changes max block size to 3MB")
        self.reconnect_p2p(node)
        chaintips = node.getchaintips()
        assert_equal(len(chaintips), 5)

        self.block_time += 1
        blocknew = self.new_block(29, spend=self.unspent[9])
        blocknew.xfieldType = 2
        blocknew.xfield = int(3 * MAX_BLOCK_BASE_SIZE)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B30 -- Create block - block size 3MB - sign with aggpubkey2 -- success
        self.block_time += 1
        blocknew = self.new_block(30, spend=self.unspent[10])
        self.expand_block(blocknew, 3 * MAX_BLOCK_BASE_SIZE)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B31 -  Create block - block MAX_BLOCK_SIGOPS - sign with aggpubkey1 -- success
        self.log.info("Accept a block with MAX_BLOCK_SIGOPS checksigs")
        self.block_time += 1
        blocknew = self.new_block(31, spend=self.unspent[11], script=CScript([OP_CHECKSIG] * ( 3 * 20000)))
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B -  Create block - block MAX_BLOCK_SIGOPS +1 - sign with aggpubkey1 -- failure
        self.log.info("Reject a block with too many checksigs")
        self.block_time += 1
        blocknew = self.new_block(32, spend=self.unspent[12], script=CScript([OP_CHECKSIG] * ((3 * 20000) + 1)))
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, reject_code=16, reject_reason="bad-blk-sigops")
        assert_equal(self.tip, node.getbestblockhash())

        self.reconnect_p2p(node)

        #B32 - B35 -- Generate 4 blocks - no change in aggpubkey or block size -- chain becomes longer
        self.unspent = self.unspent + generate_blocks(4, node, self.coinbase_pubkey, self.aggprivkey[1])
        self.tip  = node.getbestblockhash()
        tip_after_invalidate = self.tip

        #B -- Create block - block size 3MB + 1 - sign with aggpubkey2 -- failure - max block size exceeded
        self.block_time += 1
        blocknew = self.new_block(36, spend=self.unspent[12])
        self.expand_block(blocknew, (3 * MAX_BLOCK_BASE_SIZE) + 1)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(self.tip, node.getbestblockhash())

        #B36 -- Create block - block size 1MB + 1 - sign with aggpubkey2 -- success
        self.reconnect_p2p(node)

        self.block_time += 1
        blocknew = self.new_block(36, spend=self.unspent[12])
        self.expand_block(blocknew, MAX_BLOCK_BASE_SIZE + 1)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        invalidate = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B37 -- Create block - block size 0.5MB - sign with aggpubkey2 -- success
        self.block_time += 1
        blocknew = self.new_block(37, spend=self.unspent[13])
        self.expand_block(blocknew, int(MAX_BLOCK_BASE_SIZE/2))
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #call invalidate block rpc on B36 -- success - B37 is removed from the blockchain. tip is B35
        self.log.info("Test invalidate block")
        node.invalidateblock(invalidate)
        self.tip = tip_after_invalidate
        assert_equal(self.tip, node.getbestblockhash())

        #B36 -- Re Create a new block B36 -- success
        self.block_time += 1
        blocknew = self.new_block(36, spend=self.unspent[14])
        self.expand_block(blocknew, int(MAX_BLOCK_BASE_SIZE/2))
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B37 -- Re Create a new block B37 -- success
        self.reconnect_p2p(node)
        self.block_time += 1
        blocknew = self.new_block(37, spend=self.unspent[15])
        self.expand_block(blocknew, int(MAX_BLOCK_BASE_SIZE/2))
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B -- - Create block with 1 invalid transaction and new maxblocksize - sign with aggpubkey2 -- failure and new maxblocksize is not added to the list
        self.reconnect_p2p(node)
        self.block_time += 1
        blocknew = self.new_block(38, spend=self.unspent[10])
        blocknew.vtx[1].vout[0].amount = 1000
        blocknew.xfieldType = 2
        blocknew.xfield = int(4 * MAX_BLOCK_BASE_SIZE)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)

        self.reconnect_p2p(node)
        assert_equal(self.tip, node.getbestblockhash())

        #B38 - B42 -- Generate 5 blocks - no change in aggpubkey or block size -- chain becomes longer
        self.unspent = self.unspent + generate_blocks(5, node, self.coinbase_pubkey, self.aggprivkey[1])
        self.tip  = node.getbestblockhash()
        self.sync_all([self.nodes[0:2]])
        assert_equal(self.tip, syn_node.getbestblockhash())
        assert(node.getblock(self.tip))

        self.log.info("Accept block which changes max block size to 4MB")
        #B43 -- set block length to 4 MB -- sign with aggpubkey1 -- success
        self.block_time += 1
        blocknew = self.new_block(43, spend=self.unspent[16])
        blocknew.xfieldType = 2
        blocknew.xfield = int(4 * MAX_BLOCK_BASE_SIZE)
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        b43 = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B44 -- send block of 4 MB -- sign with aggpubkey1 -- success
        self.block_time += 1
        blocknew = self.new_block(44, spend=self.unspent[17])
        self.expand_block(blocknew, int(4 * MAX_BLOCK_BASE_SIZE))
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True)
        self.tip = blocknew.hash
        reorg_start = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        self.log.info("Verifying getblockchaininfo")
        expectedAggPubKeys = [
            { self.aggpubkeys[0] : 0},
            { self.aggpubkeys[1] : 17}
        ]
        expectedblockheights = [
            { '1000000': 0},
            { '500000' : 18},
            { '3000000' : 30},
            { '4000000' : 44}
        ]
        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)
        assert_equal(blockchaininfo["maxBlockSizes"], expectedblockheights)
        self.sync_all([self.nodes[0:2]])

        self.stop_node(0)
        self.log.info("Restarting node0 with '-reindex'")
        self.start_node(0, extra_args=["-reindex"], timeout=TAPYRUSD_REORG_TIMEOUT)
        connect_nodes(self.nodes[0], 1)
        self.reconnect_p2p(node)
        wait_until(lambda: self.nodes[0].getblockcount() >= 44, timeout=TAPYRUSD_REORG_TIMEOUT)
        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)
        assert_equal(blockchaininfo["maxBlockSizes"], expectedblockheights)
        self.connectNodeAndCheck(2, expectedAggPubKeys, expectedblockheights)

        #B45 - B47 -- Generate 3 blocks - no change in aggpubkey or block size -- chain becomes longer
        chaintips = node.getchaintips()
        assert_equal(len(chaintips), 1)
        self.unspent = self.unspent + generate_blocks(3, node, self.coinbase_pubkey, self.aggprivkey[1])
        tip_before_reorg = node.getbestblockhash()
        self.sync_all([self.nodes[0:2]])

        self.log.info("Simulate Blockchain Reorg  - After the last block size change")
        self.block_time += 1
        self.tip = reorg_start

        #B45  -- Create block with previous block hash = B44 - sign with aggpubkey1 -- success - block is accepted but there is no re-org
        blocknew = self.new_block(45, spend=self.unspent[18])
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(tip_before_reorg, node.getbestblockhash())
        self.tip = blocknew.hash

        #B46 -- Create block with previous block hash = B45 - sign with aggpubkey1 -- success - block is accepted but there is no re-org
        self.block_time += 1
        blocknew = self.new_block(46, spend=self.unspent[19])
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(tip_before_reorg, node.getbestblockhash())
        self.tip = blocknew.hash

        #B47 -- Create block with previous block hash = B46 - sign with aggpubkey1 -- success - block is accepted but there is no re-org
        self.block_time += 1
        blocknew = self.new_block(47, spend=self.unspent[20])
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=False, request_block=False)
        assert_equal(tip_before_reorg, node.getbestblockhash())
        self.tip = blocknew.hash

        #B48 -- Create block with previous block hash = B47 - sign with aggpubkey1 -- success - block is accepted and re-org happens
        # Reconnect P2P before using send_message directly (connection may have dropped during previous operations)
        self.block_time += 1
        blocknew = self.new_block(48, spend=self.unspent[21])
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True, request_block=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        self.sync_all([self.nodes[0:2]])

        #B49 -- Create block with previous block hash = B48 - sign with aggpubkey1 -- success - block is accepted
        self.block_time += 1
        blocknew = self.new_block(49, spend=self.unspent[22])
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True, request_block=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        #B50 -- Create block with previous block hash = B49 - sign with aggpubkey1 -- success - block is accepted
        self.block_time += 1
        blocknew = self.new_block(50, spend=self.unspent[23])
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True, request_block=True)
        self.tip = blocknew.hash
        tip_before_reorg = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())

        self.log.info("Verifying getblockchaininfo")
        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)
        assert_equal(blockchaininfo["maxBlockSizes"], expectedblockheights)

        #call invalidate block rpc on B43, B16 -- failure
        assert_raises_rpc_error(-8, "Cannot invalidate block as Xfield change found in chain after this block", node.invalidateblock, b16)
        assert_raises_rpc_error(-8, "Cannot invalidate block as Xfield change found in chain after this block", node.invalidateblock, b43)

        self.stop_node(0)
        self.log.info("Restarting node0 with '-reindex'")
        self.start_node(0, extra_args=["-reindex"], timeout=TAPYRUSD_REORG_TIMEOUT)
        connect_nodes(self.nodes[0], 1)
        self.reconnect_p2p(node)
        self.connectNodeAndCheck(2, expectedAggPubKeys, expectedblockheights)

        #B51 -- success
        self.block_time += 1
        blocknew = self.new_block(51, spend=self.unspent[24])
        blocknew.solve(self.aggprivkey[1])
        node.p2p.send_blocks_and_test([blocknew], node, success=True, request_block=True)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        self.sync_all([self.nodes[0:2]])
        assert_equal(self.tip, syn_node.getbestblockhash())

        chaintips = node.getchaintips()
        assert_equal(len(chaintips), 1)

        self.log.info("Verifying getblockchaininfo")
        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)
        assert_equal(blockchaininfo["maxBlockSizes"], expectedblockheights)

        self.log.info("Restarting node0 with '-reindex'")
        self.stop_node(0)
        self.start_node(0, extra_args=["-reindex"], timeout=TAPYRUSD_REORG_TIMEOUT)
        connect_nodes(self.nodes[0], 1)
        self.reconnect_p2p(node)
        wait_until(lambda: self.nodes[0].getblockcount() >= 51, timeout=TAPYRUSD_REORG_TIMEOUT)
        self.sync_all([self.nodes[0:2]])
        self.stop_node(0)
        self.stop_node(1)

       # copy blocks directorry to other nodes to test -loadblock, -reindex and -reloadxfield
        shutil.copyfile(os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'), os.path.join(self.nodes[0].datadir, 'blk00000.dat'))
        os.mkdir(os.path.join(self.nodes[1].datadir, 'blocks'))
        shutil.copyfile(os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'), os.path.join(self.nodes[1].datadir, 'blocks', 'blk00000.dat'))
        os.mkdir(os.path.join(self.nodes[2].datadir, 'blocks'))
        shutil.copyfile(os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'), os.path.join(self.nodes[2].datadir, 'blocks', 'blk00000.dat'))
        os.remove(os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'))

        self.log.info("Restarting nodes 0 with '-loadblock' and 'reindex-chainstate'")
        shutil.copyfile(os.path.join(self.nodes[0].datadir, 'blk00000.dat'), os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'))
        self.start_node(0, extra_args=["-reindex-chainstate"], timeout=TAPYRUSD_REORG_TIMEOUT)
        self.reconnect_p2p(node)
        wait_until(lambda: self.nodes[0].getblockcount() >= 51, timeout=TAPYRUSD_REORG_TIMEOUT)
        blockchaininfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)

        self.log.info("Restarting nodes 1 with '-loadblock'")
        self.start_node(1, ["-loadblock=%s" % os.path.join(self.nodes[1].datadir,'blocks', 'blk00000.dat')], timeout=TAPYRUSD_REORG_TIMEOUT)
        self.reconnect_p2p(self.nodes[1])
        wait_until(lambda: self.nodes[1].getblockcount() >= 51, timeout=TAPYRUSD_REORG_TIMEOUT)
        blockchaininfo = self.nodes[1].getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)

        self.log.info("Starting node2 with '-reloadxfield'")
        self.start_node(2, ["-reloadxfield"], timeout=TAPYRUSD_REORG_TIMEOUT)
        self.reconnect_p2p(self.nodes[2])
        blockchaininfo = self.nodes[2].getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)

        #finally add the new node to the newtork and check if it can synch
        self.log.info("Add new node to the network")
        self.start_node(3)
        connect_nodes(self.nodes[1], 3)
        connect_nodes(self.nodes[2], 3)
        wait_until(lambda: self.nodes[3].getblockcount() >= 51, timeout=TAPYRUSD_REORG_TIMEOUT)
        # Allow extra time for sync after all nodes join network with large blocks
        self.log.info("Starting sync_all")
        self.sync_all([self.nodes[0:4]])
        self.log.info("Check all nodes has same state")
        for n in self.nodes:
            blockchaininfo = n.getblockchaininfo()
            assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)
            assert_equal(blockchaininfo["maxBlockSizes"], expectedblockheights)
            assert_equal(blockchaininfo["blocks"], 51)

    def connectNodeAndCheck(self, n, expectedAggPubKeys, expectedblockheights):
        #this function tests HEADERS message processing in node 'n'
        self.start_node(n)
        connect_nodes(self.nodes[0], n)
        self.sync_all([self.nodes[0:n+1]])
        blockchaininfo = self.nodes[n].getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)
        assert_equal(blockchaininfo["maxBlockSizes"], expectedblockheights)
        self.stop_node(n)

    def create_tx_with_script(self, prevtx, n, script_sig=b"", amount = 1, script_pub_key=CScript()):
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(prevtx, n), script_sig, 0xffffffff))
        tx.vout.append(CTxOut(amount, script_pub_key))
        tx.calc_sha256()
        return tx

    def update_block(self, block, new_transactions):
        [tx.rehash() for tx in new_transactions]
        block.vtx.extend(new_transactions)
        block.calc_sha256()
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.solve(self.signblockprivkey)
        assert(block.is_valid())
        return block

    def new_block(self, height, spend=None, script=CScript([OP_TRUE]), coinbase_pubkey= None, solve=True, signblockpubkey=""):
        bh = uint256_from_str(reverse_bytes(hex_str_to_bytes(self.tip)))
        block_time = self.block_time + 1
        # First create the coinbase
        coinbase = create_coinbase(height, coinbase_pubkey)
        coinbase.rehash()
        if spend is None:
            block = create_block(bh, coinbase, block_time, signblockpubkey)
        else:
            block = create_block(bh, coinbase, block_time, signblockpubkey)
            tx = self.create_tx_with_script(spend.malfixsha256, 0, script_pub_key=script, amount=1)
            self.sign_tx(tx, spend)
            self.update_block(block, [tx])
        if solve:
            block.solve(self.signblockprivkey)
        return block

    def expand_block(self, block, blocksize = MAX_BLOCK_BASE_SIZE):
        expand = blocksize - len(block.serialize())
        i = 1
        scr_size = len(ser_compact_size(MAX_SCRIPT_SIZE))
        while expand > MAX_SCRIPT_SIZE:
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(block.vtx[i].malfixsha256, 0)))
            tx.vout.append(CTxOut(1, CScript([b'\x51'])))

            script_output = CScript([b'\x6a', b'\x51' * (MAX_SCRIPT_SIZE - scr_size - 1)])
            tx.vout.append(CTxOut(0, script_output))

            spend = {}
            spend['scriptPubKey'] = bytes_to_hex_str(block.vtx[i].vout[0].scriptPubKey)
            expand = expand - len(tx.serialize())
            block = self.update_block(block, [tx])
            i = i + 1

        if expand > 0:
            tx = CTransaction()
            tx.vin.append(CTxIn(COutPoint(block.vtx[i].malfixsha256, 0)))
            tx.vout.append(CTxOut(1, CScript([b'\x51'])))
            script_output = CScript([b'\x6a'])
            tx.vout.append(CTxOut(0, script_output))

            spend = {}
            spend['scriptPubKey'] = bytes_to_hex_str(block.vtx[i].vout[0].scriptPubKey)
            expand = expand - len(tx.serialize()) - len(ser_compact_size(expand))
            if len(block.serialize()) < 1048576:
                expand = expand - 2
            elif len(block.serialize()) < 16777216:
                expand = expand - 4
            script_output = CScript([b'\x6a', b'\x51'* expand])
            tx.vout[1].scriptPubKey = script_output
            block = self.update_block(block, [tx])
        #assert_equal(len(block.serialize()), blocksize)
        return block

    def sign_tx(self, tx, spend):
        scriptPubKey = CScript(spend.vout[0].scriptPubKey)
        if (scriptPubKey[0] == OP_TRUE):  # an anyone-can-spend
            tx.vin[0].scriptSig = CScript()
            return
        (sighash, err) = SignatureHash(scriptPubKey, tx, 0, SIGHASH_ALL)
        if self.sig_scheme == 1:
            tx.vin[0].scriptSig = CScript([self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL]))])
            self.sig_scheme = 0
        else:
            tx.vin[0].scriptSig = CScript([self.schnorr_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL]))])
            self.sig_scheme = 1

if __name__ == '__main__':
    MaxBloxkSizeInXFieldTest().main()
