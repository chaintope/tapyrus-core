#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019-2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test running tapyrusd with -reindex and -reindex-chainstate options.

- Start a single node and generate 3 blocks.
- Stop the node and restart it with -reindex. Verify that the node has reindexed up to block 3.
- Stop the node and restart it with -reindex-chainstate. Verify that the node has reindexed up to block 3.

Also tests that xfield history (aggregate pubkey changes) is preserved correctly
across multiple reindex operations and that block files are not corrupted:

- Generate 3 blocks (B1-B3) signed with aggpubkey1.
- Submit a federation block (B4) that introduces aggpubkey2.
- Generate 3 more blocks (B5-B7) signed with aggpubkey2.
- Record block file sizes.
- Reindex (-reindex). Verify block count, aggregatePubkeys, and block file sizes are intact.
- Submit a federation block (B8) that introduces aggpubkey3.
- Generate 3 more blocks (B9-B11) signed with aggpubkey3.
- Record block file sizes.
- Reindex again (-reindex). Verify block count, aggregatePubkeys, and block file sizes are intact.
- Generate 3 more blocks to confirm no block file corruption.
"""
import os
import time

from test_framework.blocktools import create_block, create_coinbase, createTestGenesisBlock
from test_framework.timeout_config import TAPYRUSD_REORG_TIMEOUT
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, bytes_to_hex_str, wait_until, NetworkDirName


class ReindexTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.aggpubkeys = [
            "025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
            "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc",
            "02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf",
        ]
        self.aggprivkey = [
            "67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37",
            "dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b",
            "aa2c70c4b85a09be514292d04b27bbb0cc3f86d306d58fe87743d10a095ada07",
        ]
        self.aggprivkey_wif = [
            "cR4F4fGuKjDWxiYDtGtyM77WkrVhTgokVyM2ERxoxp7R4RQP9dgE",
            "cUwpWhH9CbYwjUWzfz1UVaSjSQm9ALXWRqeFFiZKnn8cV6wqNXQA",
            "cTHVmjaAwKtU75t89fg42SLx43nRxhsri6YY1Eynvs1V1tPRCfae",
        ]
        self.genesisBlock = createTestGenesisBlock(self.aggpubkeys[0], self.aggprivkey[0], int(time.time() - 100))

    def reindex(self, justchainstate=False):
        self.nodes[0].generate(3, self.signblockprivkey_wif)
        blockcount = self.nodes[0].getblockcount()
        self.stop_nodes()
        extra_args = [["-reindex-chainstate" if justchainstate else "-reindex"]]
        self.start_nodes(extra_args)
        wait_until(lambda: self.nodes[0].getblockcount() == blockcount)
        self.log.info("Success")

    def submit_federation_block(self, node, height, new_aggpubkey, signing_privkey):
        """Create and submit a federation block introducing a new aggregate pubkey."""
        tip = node.getbestblockhash()
        block_time = node.getblock(tip)["time"] + 1
        blocknew = create_block(int(tip, 16), create_coinbase(height), block_time, new_aggpubkey)
        blocknew.solve(signing_privkey)
        result = node.submitblock(bytes_to_hex_str(blocknew.serialize()))
        assert result is None or result == "duplicate", f"submitblock failed: {result}"
        assert_equal(blocknew.hash, node.getbestblockhash())

    def get_block_file_sizes(self, node):
        """Return a dict of {filename: size} for all blk*.dat files in the node's blocks dir."""
        blocks_dir = os.path.join(node.datadir, NetworkDirName(), "blocks")
        sizes = {}
        for fname in os.listdir(blocks_dir):
            if fname.startswith("blk") and fname.endswith(".dat"):
                sizes[fname] = os.path.getsize(os.path.join(blocks_dir, fname))
        return sizes

    def reindex_and_verify_xfield(self, expected_aggpubkeys, expected_blockcount, sizes_before):
        """Restart with -reindex, then verify block count, xfield history, and block file sizes."""
        self.stop_nodes()
        self.start_nodes([["-reindex"]])
        wait_until(lambda: self.nodes[0].getblockcount() >= expected_blockcount, timeout=TAPYRUSD_REORG_TIMEOUT)
        assert_equal(self.nodes[0].getblockcount(), expected_blockcount)
        blockchaininfo = self.nodes[0].getblockchaininfo()
        assert_equal(blockchaininfo["aggregatePubkeys"], expected_aggpubkeys)
        sizes_after = self.get_block_file_sizes(self.nodes[0])
        assert_equal(sizes_before, sizes_after)

    def run_test(self):
        self.log.info("Test basic reindex and reindex-chainstate")
        self.reindex(False)
        self.reindex(True)
        self.reindex(False)
        self.reindex(True)

        self.log.info("Test xfield history and block file integrity across reindex")
        node = self.nodes[0]

        # B1-B3: Generate 3 blocks with aggpubkey1
        node.generate(3, self.aggprivkey_wif[0])
        assert_equal(node.getblockcount(), 15)

        # B4: Federation block introducing aggpubkey2
        self.submit_federation_block(node, 16, self.aggpubkeys[1], self.aggprivkey[0])
        assert_equal(node.getblockcount(), 16)

        # B5-B7: Generate 3 blocks signed with aggpubkey2
        node.generate(3, self.aggprivkey_wif[1])
        assert_equal(node.getblockcount(), 19)

        expected = [
            {self.aggpubkeys[0]: 0},
            {self.aggpubkeys[1]: 17},
        ]
        assert_equal(node.getblockchaininfo()["aggregatePubkeys"], expected)

        # Record block file sizes before first reindex
        sizes_before_first = self.get_block_file_sizes(node)
        self.log.info(f"Block file sizes before first reindex: {sizes_before_first}")

        # First reindex: verify xfield history, block count, and file sizes
        self.log.info("First -reindex with xfield history")
        self.reindex_and_verify_xfield(expected, 19, sizes_before_first)
        self.log.info("Block file sizes unchanged after first reindex")

        # B8: Federation block introducing aggpubkey3 (proves no file corruption)
        self.submit_federation_block(node, 20, self.aggpubkeys[2], self.aggprivkey[1])
        assert_equal(node.getblockcount(), 20)

        # B9-B11: Generate 3 blocks signed with aggpubkey3
        node.generate(3, self.aggprivkey_wif[2])
        assert_equal(node.getblockcount(), 23)

        expected = [
            {self.aggpubkeys[0]: 0},
            {self.aggpubkeys[1]: 17},
            {self.aggpubkeys[2]: 21},
        ]
        assert_equal(node.getblockchaininfo()["aggregatePubkeys"], expected)

        # Record block file sizes before second reindex
        sizes_before_second = self.get_block_file_sizes(node)
        self.log.info(f"Block file sizes before second reindex: {sizes_before_second}")

        # Second reindex: verify xfield history, block count, and file sizes
        self.log.info("Second -reindex with xfield history")
        self.reindex_and_verify_xfield(expected, 23, sizes_before_second)
        self.log.info("Block file sizes unchanged after second reindex")

        # Generate 3 more blocks to confirm the node is fully functional
        node.generate(3, self.aggprivkey_wif[2])
        assert_equal(node.getblockcount(), 26)
        self.log.info("Success")


if __name__ == '__main__':
    ReindexTest().main()
