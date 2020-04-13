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
B -- - Create block with 1 invalid transaction and aggpubkey3 - sign with aggpubkey2 -- failure and aggpubkey3 is not added to the list
B24 -- Create block with 1 valid transaction and aggpubkey3- sign with aggpubkey2 -- success and aggpubkey3 is added to the list
B25 -- Create block with 1 valid transaction - sign with aggpubkey3 -- success
Simulate Blockchain Reorg as follows -- 
B24 -- Create block with previous block hash = B23 - sign with aggpubkey2 -- success - block is accepted but there is no re-org
B25 -- Create block with previous block hash = B24 - sign with aggpubkey2 -- success - block is accepted but there is no re-org
B26 -- Create block with previous block hash = B25 - sign with aggpubkey2 -- success - there is a chain re-org - aggpubkey3 is removed from the list
B27 -- Create block with previous block hash = B26 and aggpubkey4 - sign with aggpubkey2 -- success - aggpubkey4 is added to the list
B -- Create block with previous block hash = B27 - sign with aggpubkey2 -- failure - proof verification failed
B -- Create block with previous block hash = B27 - sign with aggpubkey3 -- failure - proof verification failed
B28 -- Create block with previous block hash = B27 - sign with aggpubkey4 -- success
B29 - B30 -- Generate 2 blocks - no aggpubkey -- chain becomes longer
B31 -- Create block with aggpubkey5 - sign using aggpubkey4 -- success - aggpubkey5 is added to the list
call invalidate block rpc on B31 -- success - B31 is removed from the blockchain. aggpubkey5 is removed from the list. tip is B30
test the output of RPC Getblockchaininfo before reorg and after reorg. Verify block aggpubkey and block height
Before reorg after B25 the expected output is
aggpubkey1 = 0
aggpubkey2 = 12
aggpubkey3 = 26

After reorg after B27 the expected output is
aggpubkey1 = 0
aggpubkey2 = 12
aggpubkey4 = 28

Restart the node with -reindex option. This triggers a full rewind of block index. Verify that the tip reaches B30 at the end.
"""
import shutil, os
import time

from test_framework.blocktools import create_block, create_coinbase, create_tx_with_script,  createTestGenesisBlock
from test_framework.key import CECKey
from test_framework.schnorr import Schnorr
from test_framework.mininode import P2PDataStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, bytes_to_hex_str
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
        node.add_p2p_connection(P2PDataStore())
        node.p2p.wait_for_getheaders(timeout=5)

        self.log.info("Test starting...")

        #genesis block
        self.tip = int(self.genesisBlock.hash, 16)
        self.blocks = [ self.tip ]
        block_time = self.genesisBlock.nTime

        # Create a new blocks B1 - B10
        self.blocks += node.generate(10, self.aggprivkey[0])

        self.log.info("First federation block")
        # B11 - Create block - aggpubkey2 - sign with aggpubkey1
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(11), block_time, self.aggpubkeys[1])
        blocknew.solve(self.aggprivkey[0])
        self.blocks += [ blocknew.sha256 ]

        node.p2p.send_blocks_and_test([blocknew], node, True)
        #res = node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        #print(res)
        self.tip = self.blocks[-1]
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B -- Create block with invalid aggpubkey2 - sign with aggpubkey1 -- failure - invalid aggpubkey
        self.aggpubkeys[-1][4:2] = "00"
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(12), block_time, self.aggpubkeys[-1])
        blocknew.solve(self.aggprivkey[0])
        assert_raises_rpc_error(-22, "Invalid aggregatePubkey", node.submitblock, bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        # B - Create block - sign with aggpubkey1 - failure
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(12), block_time)
        blocknew.solve(self.aggprivkey[0])
        assert_raises_rpc_error(-22, "Proof verification failed", node.submitblock, bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        # B12 - Create block - sign with aggpubkey2
        blocknew.solve(self.aggprivkey[1])
        self.blocks += [ blocknew.sha256 ]

        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[-1]
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        # Create a new blocks B13 - B22
        self.blocks += node.generate(10, self.aggprivkey[1])

        #B23 -- Create block with 1 valid transaction - sign with aggpubkey2 -- success
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(23), block_time)
        coinbaseSpent = node.getblock(self.blocks[11])['tx'][0]
        tx = create_and_sign_transaction(coinbaseSpent, 10)
        add_transactions_to_block(blocknew, tx)
        blocknew.solve(self.aggprivkey[1])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #call invalidate block rpc on B23 -- success - B23 is removed from the blockchain. tip is B22
        node.invalidateblock(self.tip)
        self.tip = self.blocks[22]
        assert_equal(self.tip, node.getbestblockhash())

        #B23 -- Re Create a new block B23 -- success
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(23), block_time)
        coinbaseSpent = node.getblock(self.blocks[12])['tx'][0]
        tx = create_and_sign_transaction(coinbaseSpent, 10)
        add_transactions_to_block(blocknew, tx)
        blocknew.solve(self.aggprivkey[1])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[23] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B -- - Create block with 1 invalid transaction - sign with aggpubkey2 -- failure
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(23), block_time)
        coinbaseSpent = node.getblock(self.blocks[12])['tx'][0]
        tx = create_and_sign_transaction(coinbaseSpent, 100) #invalid tx
        add_transactions_to_block(blocknew, tx)
        blocknew.solve(self.aggprivkey[1])
        assert_raises_rpc_error(-22, "Proof verification failed", node.submitbloc, kbytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        #B -- - Create block with 1 invalid transaction and aggpubkey3 - sign with aggpubkey2 -- failure and aggpubkey3 is not added to the list
        blocknew = create_block(self.tip, create_coinbase(23), block_time, self.aggpubkey[2])
        coinbaseSpent = node.getblock(self.blocks[13])['tx'][0]
        tx = create_and_sign_transaction(coinbaseSpent, 100) #invalid tx
        add_transactions_to_block(blocknew, tx)
        blocknew.solve(self.aggprivkey[1])
        assert_raises_rpc_error(-16, "bad-txns-txouttotal-toolarge", node.submitblock, bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        # verify aggpubkey3 is not added to the list : verify that block signed using aggprickey3 is rejected
        blocknew = create_block(self.tip, create_coinbase(24, block_time, self.aggpubkey[2]))
        blocknew.solve(self.aggprivkey[2])
        assert_raises_rpc_error(-22, "Proof verification failed", node.submitblock, bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        self.log.info("Second federation block")
        #B24 -- Create block with 1 valid transaction and aggpubkey3- sign with aggpubkey2 -- success and aggpubkey3 is added to the list
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(24), block_time, self.aggpubkey[2])
        coinbaseSpent = node.getblock(self.blocks[13])['tx'][0]
        tx = create_and_sign_transaction(coinbaseSpent, 100) #invalid tx
        add_transactions_to_block(blocknew, tx)
        blocknew.solve(self.aggprivkey[1])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[24] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B25 -- Create block with 1 valid transaction - sign with aggpubkey3 -- success
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(25), block_time)
        coinbaseSpent = node.getblock(self.blocks[14])['tx'][0]
        tx = create_and_sign_transaction(coinbaseSpent, 10)
        add_transactions_to_block(blocknew, tx)
        blocknew.solve(self.aggprivkey[2])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[25] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        self.log.info("Verifying getblockchaininfo")
        #getblockchaininfo
        expectedAggPubKeys = {
            self.aggpubkey[0] : 0,
            self.aggpubkey[1] : 12,
            self.aggpubkey[2] : 26}
        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubKeys"], expectedAggPubKeys)

        self.log.info("Simulate Blockchain Reorg")
        #B24 -- Create block with previous block hash = B23 - sign with aggpubkey2 -- success - block is accepted but there is no re-org
        block_time += 1
        blocknew = create_block(self.blocks[23], create_coinbase(24), block_time)
        blocknew.solve(self.aggprivkey[1])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.blocks[24] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B25 -- Create block with previous block hash = B24 - sign with aggpubkey2 -- success - block is accepted but there is no re-org
        block_time += 1
        blocknew = create_block(self.blocks[24], create_coinbase(25), block_time)
        blocknew.solve(self.aggprivkey[1])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.blocks[25] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B26 -- Create block with previous block hash = B25 - sign with aggpubkey2 -- success - there is a chain re-org - aggpubkey3 is removed from the list
        block_time += 1
        blocknew = create_block(self.blocks[25], create_coinbase(26), block_time)
        blocknew.solve(self.aggprivkey[1])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[26] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        # verify aggpubkey3 is not added to the list : verify that block signed using aggprickey3 is rejected
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(27), block_time)
        blocknew.solve(self.aggprivkey[2])
        assert_raises_rpc_error(-22, "Proof verification failed", node.submitblock, bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        self.log.info("Third Federation Block")
        #B27 -- Create block with previous block hash = B26 and aggpubkey4 - sign with aggpubkey2 -- success - aggpubkey4 is added to the list
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(27), block_time, self.aggpubkey[3])
        blocknew.solve(self.aggprivkey[1])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[27] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B -- Create block with previous block hash = B27 - sign with aggpubkey2 -- failure - proof verification failed
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(28), block_time)
        blocknew.solve(self.aggprivkey[1])
        assert_raises_rpc_error(-22, "Proof verification failed", node.submitblock, bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        #B -- Create block with previous block hash = B27 - sign with aggpubkey3 -- failure - proof verification failed
        blocknew.solve(self.aggprivkey[2])
        assert_raises_rpc_error(-22, "Proof verification failed", node.submitblock,bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        #B28 -- Create block with previous block hash = B27 - sign with aggpubkey4 -- success
        blocknew.solve(self.aggprivkey[3])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[28] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #B29 - B30 -- Generate 2 blocks - no aggpubkey -- chain becomes longer
        self.blocks += node.generate(2, self.aggprivkey[3])
        self.tip = self.blocks[30]
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        self.log.info("Fourth Federation Block")
        #B31 -- Create block with aggpubkey5 - sign using aggpubkey4 -- success - aggpubkey5 is added to the list
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(31), block_time, self.aggpubkey[4])
        blocknew.solve(self.aggprivkey[3])
        node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        self.tip = self.blocks[31] = blocknew.sha256
        assert_equal(self.tip, node.getbestblockhash())
        assert(node.getblock(self.tip))

        #call invalidate block rpc on B31 -- success - B31 is removed from the blockchain. aggpubkey5 is removed from the list. tip is B30
        node.invalidateblock(self.tip)
        self.tip = self.blocks[30]
        assert_equal(self.tip, node.getbestblockhash())

        # verify aggpubkey5 is not added to the list : verify that block signed using aggprivkey5 is rejected
        block_time += 1
        blocknew = create_block(self.tip, create_coinbase(31), block_time)
        blocknew.solve(self.aggprivkey[4])
        assert_raises_rpc_error(-22, "Proof verification failed", node.submitblock, bytes_to_hex_str(blocknew.serialize()))
        assert_equal(self.tip, node.getbestblockhash())

        self.log.info("Verifying getblockchaininfo")
        #getblockchaininfo
        expectedAggPubKeys = {
            self.aggpubkey[0] : 0,
            self.aggpubkey[1] : 12,
            self.aggpubkey[3] : 28}
        blockchaininfo = node.getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubKeys"], expectedAggPubKeys)

        self.log.info("Restarting node with '-reindex'")
        self.stop_node(0)
        self.start_node(0, extra_args=["-reindex"])

        self.log.info("Restarting node with '-loadblock'")
        self.stop_node(0)
        shutil.copyfile(os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'), os.path.join(self.nodes[0].datadir, 'blk00000.dat'))
        os.remove(os.path.join(self.nodes[0].datadir, NetworkDirName(), 'blocks', 'blk00000.dat'))
        self.start_node(0, extra_args=["-loadblock=%s" % os.path.join(self.nodes[0].datadir, 'blk00000.dat')])

    # Helper methods
    ################

    def add_transactions_to_block(self, block, tx_list):
        [tx.rehash() for tx in tx_list]
        block.vtx.extend(tx_list)

    def create_tx(self, spend_tx, n, value, script=CScript([OP_TRUE, OP_DROP] * 15 + [OP_TRUE])):
        return create_tx_with_script(spend_tx, n, amount=value, script_pub_key=script)

    # sign a transaction, using the key we know about
    # this signs input 0 in tx, which is assumed to be spending output n in spend_tx
    def sign_tx(self, tx, spend_tx):
        scriptPubKey = bytearray(spend_tx.vout[0].scriptPubKey)
        if (scriptPubKey[0] == OP_TRUE):  # an anyone-can-spend
            tx.vin[0].scriptSig = CScript()
            return
        (sighash, err) = SignatureHash(spend_tx.vout[0].scriptPubKey, tx, 0, SIGHASH_ALL)
        if self.sig_scheme == 1:
            tx.vin[0].scriptSig = CScript([self.coinbase_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL]))])
            self.sig_scheme = 0
        else:
            tx.vin[0].scriptSig = CScript([self.schnorr_key.sign(sighash) + bytes(bytearray([SIGHASH_ALL]))])
            self.sig_scheme = 1

    def create_and_sign_transaction(self, spend_tx, value, script=CScript([OP_1])):
        tx = self.create_tx(spend_tx, 0, value, script)
        self.sign_tx(tx, spend_tx)
        tx.rehash()
        return tx

if __name__ == '__main__':
    FederationManagementTest().main()
