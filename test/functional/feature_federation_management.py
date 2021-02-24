#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test federation management block processing.

B0 -- Genesis block -- aggpubkey1
B1 - B10 -- Generate 10 blocks with no aggpubkey -- chain becomes longer
B11 -- Create block - aggpubkey2 - sign with aggpubkey1 -- success - aggpubkey2 is added to the list
B -- Create block - sign with aggpubkey1 -- failure - proof verification fails
B -- Create block with invalid aggpubkey2 - sign with aggpubkey1 -- failure - invalid aggpubkey
B12 -- Create block - sign with aggpubkey2 -- success
B13 - B22 -- Generate 10 blocks - no aggpubkey -- chain becomes longer
B23 -- Create block with 1 valid transaction - sign with aggpubkey2 -- success
call invalidate block rpc on B23 -- success - B23 is removed from the blockchain. tip is B22
B23 -- Re Create a new block B23 -- success
B -- - Create block with 1 invalid transaction - sign with aggpubkey2 -- failure
B -- - Create block with 1 invalid transaction and aggpubkey3 - sign with aggpubkey2 -- failure and aggpubkey3 is not added to the list : verify that block signed using aggprivkey3 is rejected
B24 -- Create block with 1 valid transaction and aggpubkey3- sign with aggpubkey2 -- success and aggpubkey3 is added to the list
B25 -- Create block with 1 valid transaction - sign with aggpubkey3 -- success
B26 - B30 - Generate 5 new blocks 

Simulate Blockchain Reorg  - After the last federation block
B27 -- Create block with previous block hash = B26 - sign with aggpubkey3 -- success - block is accepted but there is no re-org
B28 -- Create block with previous block hash = B27 - sign with aggpubkey3 -- success - block is accepted but there is no re-org
B29 -- Create block with previous block hash = B28 - sign with aggpubkey3 -- success - block is accepted but there is no re-org
B30 -- Create block with previous block hash = B29 - sign with aggpubkey3 -- success - block is accepted but there is no re-org
B31 -- Create block with previous block hash = B30 - sign with aggpubkey3 -- success - block is accepted and re-org happens

Simulate Blockchain Reorg - Before the last federation block"
B24 -- Create block with previous block hash = B23 - sign with aggpubkey2 -- failure - block is in invalid chain
B25 -- Create block with previous block hash = B24 - sign with aggpubkey2 -- success - block is in invalid chain
there are 3 tips in the current blockchain

B32 -- Create block with aggpubkey4 - sign with aggpubkey3 -- success - aggpubkey4 is added to the list
B -- Create block - sign with aggpubkey2 -- failure - proof verification failed
B -- Create block - sign with aggpubkey3 -- failure - proof verification failed
B33 -- Create block  - sign with aggpubkey4 -- success
B34 - B35 -- Generate 2 blocks - no aggpubkey -- chain becomes longer
B36 -- Create block with aggpubkey5 - sign using aggpubkey4 -- success - aggpubkey5 is added to the list
call invalidate block rpc on B36 -- failure - B36 is a federation block
B37 - Create block - sign using aggpubkey5 -- success

test the output of RPC Getblockchaininfo before reorg and after reorg. Verify block aggpubkey and block height
After B25 the expected output is
aggpubkey1 = 0
aggpubkey2 = 12
aggpubkey3 = 25

After B37 the expected output is
aggpubkey1 = 0
aggpubkey2 = 12
aggpubkey3 = 25
aggpubkey4 = 33
aggpubkey5 = 37

Restart the node with -reindex, -reindex-chainstate and -loadblock options. This triggers a full rewind of block index. Verify that the tip reaches B37 at the end.
"""
import shutil, os
import time

from test_framework.blocktools import create_block, create_coinbase, create_tx_with_script,  createTestGenesisBlock, create_transaction
from test_framework.key import CECKey
from test_framework.schnorr import Schnorr
from test_framework.mininode import P2PDataStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, bytes_to_hex_str, assert_raises_rpc_error, NetworkDirName
from test_framework.script import CScript, OP_TRUE, OP_DROP, OP_1

class FederationManagementTest(BitcoinTestFramework):
    def set_test_params(self):
        self.aggpubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc",
        "02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf",
        "03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b",
        "02b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a",
        "02e78cafe033b22bda5d7d1c8e82ee932930bf12e08489bc19769cbec765568be9",
        "02473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"]
        
        self.aggprivkey = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37",
        "dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b",
        "aa2c70c4b85a09be514292d04b27bbb0cc3f86d306d58fe87743d10a095ada07",
        "3087d8decc5f951f19a442397cf1eba1e2b064e68650c346502780b56454c6e2",
        "6125c8d4330941944cc6cc3e775e8620c479a5901ad627e6e734c6a6f7377428",
        "1c3e5453c0f9aa74a8eb0216310b2b013f017813a648fce364bf41dbc0b37647",
        "ea9fe9fd2f1761fc6f1f0f23eb4d4141d7b05f2b95a1b7a9912cd97bddd9036c"]

        self.aggprivkey_wif = ["cR4F4fGuKjDWxiYDtGtyM77WkrVhTgokVyM2ERxoxp7R4RQP9dgE",
        "cUwpWhH9CbYwjUWzfz1UVaSjSQm9ALXWRqeFFiZKnn8cV6wqNXQA",
        "cTHVmjaAwKtU75t89fg42SLx43nRxhsri6YY1Eynvs1V1tPRCfae",
        "cPD3D5AvmXhw7NGxQeaRhTVNW2UoYeibQAMhye7jzyM4ETG9d1ez",
        "cQqYVqYhK47EWvDViNwcyhc6sLS6tkuhED7T3rvumeGRtVJcEQHh",
        "cNXbwddRQrPR4k7Us7eSrRUHFBerNBKwxrExTSs4gdH1rjHdoNuL",
        "cVSnGe9DzWfEgahMjSXs5nuVqnwvyanG9aaEQF6m7M5mSY2wfZzW"]


        self.blocks = []

        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))

        self.schnorr_key = Schnorr()
        self.schnorr_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))

        self.num_nodes = 1
        self.sig_scheme = 0
        self.setup_clean_chain = True
        self.genesisBlock = createTestGenesisBlock(self.aggpubkeys[0], self.aggprivkey[0], int(time.time() - 100))

    def run_test(self):
        node = self.nodes[0]  # convenience reference to the node
        self.address = node.getnewaddress()
        node.add_p2p_connection(P2PDataStore())
        node.p2p.wait_for_getheaders(timeout=5)

        self.log.info("Test starting...")

        #genesis block (B0)
        self.blocks = [ self.genesisBlock.hash ]
        block_time = self.genesisBlock.nTime

        # Create a new blocks B1 - B10
        self.blocks += node.generate(10, self.aggprivkey_wif[0])
        best_block = node.getblock(node.getbestblockhash())
        self.tip = node.getbestblockhash()

        self.log.info("First federation block")
        # B11 - Create block - aggpubkey2 - sign with aggpubkey1
        block_time = best_block["time"] + 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(11), block_time, self.aggpubkeys[1])
        blocknew.solve(self.aggprivkey[0])
        self.blocks += [ blocknew.hash ]

        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[-1]
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B -- Create block with invalid aggpubkey2 - sign with aggpubkey1 -- failure - invalid aggpubkey
        aggpubkeyInv = self.aggpubkeys[-1][:-2]
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(12), block_time, aggpubkeyInv)
        blocknew.solve(self.aggprivkey[0])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())

        # B - Create block - sign with aggpubkey1 - failure - Proof verification failed
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(12), block_time)
        blocknew.solve(self.aggprivkey[0])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())

        # B12 - Create block - sign with aggpubkey2
        blocknew.solve(self.aggprivkey[1])
        self.blocks += [ blocknew.hash ]

        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[-1]
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        # Create a new blocks B13 - B22
        self.blocks += node.generate(10, self.aggprivkey_wif[1])
        best_block = node.getblock(node.getbestblockhash())
        self.tip = node.getbestblockhash()

        #B23 -- Create block with 1 valid transaction - sign with aggpubkey2 -- success
        block_time = best_block["time"] + 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(23), block_time)
        spendHash = node.getblock(self.blocks[1])['tx'][0]
        blocknew.vtx  += [create_transaction(node, spendHash, self.address, amount=1.0)]
        blocknew.hashMerkleRoot = blocknew.calc_merkle_root()
        blocknew.hashImMerkleRoot = blocknew.calc_immutable_merkle_root()
        blocknew.solve(self.aggprivkey[1])
        self.blocks.append(blocknew.hash)
        self.tip = blocknew.hash
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #call invalidate block rpc on B23 -- success - B23 is removed from the blockchain. tip is B22
        node.invalidateblock(self.tip)
        self.tip = self.blocks[22]
        assert_equal(self.tip, node.getbestblockhash())

        #B23 -- Re Create a new block B23 -- success
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(23), block_time)
        spendHash = node.getblock(self.blocks[2])['tx'][0]
        blocknew.vtx  += [create_transaction(node, spendHash, self.address, amount=49.0)]
        blocknew.hashMerkleRoot = blocknew.calc_merkle_root()
        blocknew.hashImMerkleRoot = blocknew.calc_immutable_merkle_root()
        blocknew.solve(self.aggprivkey[1])
        self.blocks[22] = blocknew.hash
        self.tip = blocknew.hash
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B -- - Create block with 1 invalid transaction - sign with aggpubkey2 -- failure
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(23), block_time)
        spendHash = node.getblock(self.blocks[3])['tx'][0]
        blocknew.vtx  += [create_transaction(node, spendHash, self.address, amount=100.0)] #invalid
        blocknew.hashMerkleRoot = blocknew.calc_merkle_root()
        blocknew.hashImMerkleRoot = blocknew.calc_immutable_merkle_root()
        blocknew.solve(self.aggprivkey[1])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())

        #B -- - Create block with 1 invalid transaction and aggpubkey3 - sign with aggpubkey2 -- failure and aggpubkey3 is not added to the list
        blocknew = create_block(int(self.tip, 16), create_coinbase(23), block_time, self.aggpubkeys[2])
        spendHash = node.getblock(self.blocks[4])['tx'][0]
        blocknew.vtx  += [create_transaction(node, spendHash, self.address, amount=100.0)] #invalid
        blocknew.hashMerkleRoot = blocknew.calc_merkle_root()
        blocknew.hashImMerkleRoot = blocknew.calc_immutable_merkle_root()
        blocknew.solve(self.aggprivkey[1])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())

        # verify aggpubkey3 is not added to the list : verify that block signed using aggprivkey3 is rejected
        blocknew = create_block(int(self.tip, 16), create_coinbase(24), block_time, self.aggpubkeys[2])
        blocknew.solve(self.aggprivkey[2])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())

        self.log.info("Second federation block")
        #B24 -- Create block with 1 valid transaction and aggpubkey3- sign with aggpubkey2 -- success and aggpubkey3 is added to the list
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(24), block_time, self.aggpubkeys[2])
        spendHash = node.getblock(self.blocks[4])['tx'][0]
        blocknew.vtx  += [create_transaction(node, spendHash, self.address, amount=10.0)]
        blocknew.hashMerkleRoot = blocknew.calc_merkle_root()
        blocknew.hashImMerkleRoot = blocknew.calc_immutable_merkle_root()
        blocknew.solve(self.aggprivkey[1])
        self.blocks.append(blocknew.hash)
        self.tip = blocknew.hash
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B25 -- Create block with 1 valid transaction - sign with aggpubkey3 -- success
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(25), block_time)
        spendHash = node.getblock(self.blocks[5])['tx'][0]
        blocknew.vtx  += [create_transaction(node, spendHash, self.address, amount=10.0)]
        blocknew.hashMerkleRoot = blocknew.calc_merkle_root()
        blocknew.hashImMerkleRoot = blocknew.calc_immutable_merkle_root()
        blocknew.solve(self.aggprivkey[2])
        self.blocks.append(blocknew.hash)
        self.tip = blocknew.hash
        b25 = blocknew.hash
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        # Create a new blocks B26 - B30
        self.blocks += node.generate(5, self.aggprivkey_wif[2])
        best_block = node.getblock(node.getbestblockhash())
        self.tip = node.getbestblockhash()

        self.log.info("Verifying getblockchaininfo")
        #getblockchaininfo
        expectedAggPubKeys = [
            { self.aggpubkeys[0] : 0},
            { self.aggpubkeys[1] : 12},
            { self.aggpubkeys[2] : 25}
        ]
        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)

        self.log.info("Simulate Blockchain Reorg  - After the last federation block")
        #B27 -- Create block with previous block hash = B26 - sign with aggpubkey3 -- success - block is accepted but there is no re-org
        block_time += 1
        self.forkblocks = self.blocks
        blocknew = create_block(int(self.blocks[26], 16), create_coinbase(27), block_time)
        blocknew.solve(self.aggprivkey[2])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.forkblocks[27] = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B28 -- Create block with previous block hash = B27 - sign with aggpubkey3 -- success - block is accepted but there is no re-org
        block_time += 1
        blocknew = create_block(int(self.forkblocks[27], 16), create_coinbase(28), block_time)
        blocknew.solve(self.aggprivkey[2])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.forkblocks[28] = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B29 -- Create block with previous block hash = B28 - sign with aggpubkey3 -- success - block is accepted but there is no re-org
        block_time += 1
        blocknew = create_block(int(self.forkblocks[28], 16), create_coinbase(29), block_time)
        blocknew.solve(self.aggprivkey[2])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.forkblocks[29] = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B30 -- Create block with previous block hash = B29 - sign with aggpubkey3 -- success - block is accepted but there is no re-org
        block_time += 1
        blocknew = create_block(int(self.forkblocks[29], 16), create_coinbase(30), block_time)
        blocknew.solve(self.aggprivkey[2])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.forkblocks[30] = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B31 -- Create block with previous block hash = B30 - sign with aggpubkey3 -- success - block is accepted and re-org happens
        block_time += 1
        blocknew = create_block(int(self.forkblocks[30], 16), create_coinbase(31), block_time)
        blocknew.solve(self.aggprivkey[2])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.forkblocks.append(blocknew.hash)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)

        self.log.info("Simulate Blockchain Reorg  - Before the last federation block")
        #B24 -- Create block with previous block hash = B23 - sign with aggpubkey2 -- failure - block is in invalid chain
        block_time += 1
        blocknew = create_block(int(self.blocks[23], 16), create_coinbase(24), block_time)
        blocknew.solve(self.aggprivkey[1])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B25 -- Create block with previous block hash = B24 - sign with aggpubkey2 -- success - block is in invalid chain
        block_time += 1
        blocknew = create_block(int(self.blocks[24], 16), create_coinbase(25), block_time)
        blocknew.solve(self.aggprivkey[1])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #there are 3 tips in the current blockchain
        chaintips = node.getchaintips()
        assert_equal(len(chaintips), 3)

        assert(node.getblock(self.blocks[12]))
        assert(node.getblock(self.blocks[25]))
        assert_raises_rpc_error(-5, "Block not found", node.getblock, blocknew.hash)

        self.log.info("Third Federation Block - active chain")
        #B32 -- Create block with aggpubkey4 - sign with aggpubkey3 -- success - aggpubkey4 is added to the list
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(32), block_time, self.aggpubkeys[3])
        blocknew.solve(self.aggprivkey[2])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.forkblocks.append(blocknew.hash)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B -- Create block - sign with aggpubkey2 -- failure - proof verification failed
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(33), block_time)
        blocknew.solve(self.aggprivkey[1])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())

        #B -- Create block - sign with aggpubkey3 -- failure - proof verification failed
        blocknew.solve(self.aggprivkey[2])
        assert_equal(node.submitblock(bytes_to_hex_str(blocknew.serialize())), "invalid")
        assert_equal(self.tip, node.getbestblockhash())

        #B33 -- Create block  - sign with aggpubkey4 -- success
        blocknew.solve(self.aggprivkey[3])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.forkblocks.append(blocknew.hash)
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B34 - B35 -- Generate 2 blocks - no aggpubkey -- chain becomes longer
        self.forkblocks += node.generate(2, self.aggprivkey_wif[3])
        self.tip = self.forkblocks[35]
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        self.log.info("Fourth Federation Block")
        #B36 -- Create block with aggpubkey5 - sign using aggpubkey4 -- success - aggpubkey5 is added to the list
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(36), block_time, self.aggpubkeys[4])
        blocknew.solve(self.aggprivkey[3])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = blocknew.hash
        self.forkblocks.append(blocknew.hash)
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #call invalidate block rpc on B36 -- failure - B36 is a federation block
        assert_raises_rpc_error(-8, "Federation block found", node.invalidateblock, self.tip)
        assert_raises_rpc_error(-8, "Federation block found", node.invalidateblock, self.forkblocks[33])
        assert_raises_rpc_error(-8, "Federation block found", node.invalidateblock, self.blocks[29])
        assert_equal(self.tip, node.getbestblockhash())

        #B37 - Create block - sign using aggpubkey5 -- success
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(37), block_time)
        blocknew.solve(self.aggprivkey[4])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = blocknew.hash
        self.forkblocks.append(blocknew.hash)
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        self.log.info("Verifying getblockchaininfo")
        #getblockchaininfo
        expectedAggPubKeys = [
            { self.aggpubkeys[0] : 0},
            { self.aggpubkeys[1] : 12},
            { self.aggpubkeys[2] : 25},
            { self.aggpubkeys[3] : 33},
            { self.aggpubkeys[4] : 37},
        ]
        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)

        #B38 - B40 -- Generate 2 blocks - no aggpubkey -- chain becomes longer
        self.forkblocks += node.generate(3, self.aggprivkey_wif[4])
        self.tip = node.getbestblockhash()
        best_block = node.getblock(self.tip)

        self.log.info("Test Repeated aggpubkeys in Federation Block")
        #B41 -- Create block with aggpubkey0 - sign using aggpubkey5 -- success - aggpubkey0 is added to the list
        block_time = best_block["time"] + 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(41), block_time, self.aggpubkeys[0])
        blocknew.solve(self.aggprivkey[4])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B42 -- Create block with aggpubkey1 - sign using aggpubkey0 -- success - aggpubkey1 is added to the list
        block_time += 1
        blocknew = create_block(int(self.tip, 16), create_coinbase(42), block_time, self.aggpubkeys[1])
        blocknew.solve(self.aggprivkey[0])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = blocknew.hash
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        self.log.info("Verifying getblockchaininfo")
        #getblockchaininfo
        expectedAggPubKeys = [
            { self.aggpubkeys[0] : 0},
            { self.aggpubkeys[1] : 12},
            { self.aggpubkeys[2] : 25},
            { self.aggpubkeys[3] : 33},
            { self.aggpubkeys[4] : 37},
            { self.aggpubkeys[0] : 42},
            { self.aggpubkeys[1] : 43},
        ]
        blockchaininfo = node.getblockchaininfo()
        print(blockchaininfo["aggregatePubkeys"])
        assert_equal(blockchaininfo["aggregatePubkeys"], expectedAggPubKeys)
        self.stop_node(0)

        self.log.info("Restarting node with '-reindex-chainstate'")
        self.start_node(0, extra_args=["-reindex-chainstate"])
        self.sync_all()
        self.stop_node(0)

        self.log.info("Restarting node with '-loadblock'")
        shutil.copyfile(os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'), os.path.join(self.nodes[0].datadir, 'blk00000.dat'))
        os.remove(os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'))
        extra_args=["-loadblock=%s" % os.path.join(self.nodes[0].datadir, 'blk00000.dat'), "-reindex"]
        self.start_node(0, extra_args)

if __name__ == '__main__':
    FederationManagementTest().main()
