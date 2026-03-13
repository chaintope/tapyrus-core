#!/usr/bin/env python3
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression test: -reindex with out-of-order blocks spanning federation key changes.

This test verifies the fix in file_io.cpp where the recursive AcceptBlock call
for out-of-order blocks must pass pxfieldHistory so that proof verification
uses the correct aggregate public key after a federation key change.

Chain layout (heights 0-20):

  Heights  0  : genesis, signed with key[0]
  Heights  1-4: regular blocks, signed with key[0]
  Height   5  : federation block, introduces key[1], signed with key[0]
  Heights  6-9: regular blocks, signed with key[1]
  Height  10  : federation block, introduces key[2], signed with key[1]
  Heights 11-14: regular blocks, signed with key[2]
  Height  15  : federation block, introduces key[3], signed with key[2]  <- IN OOO SECTION
  Heights 16-20: regular blocks, signed with key[3]

blk00000.dat layout written for the reindex test:

  [0, 1, 2, 3, 4, 5]   -- in-order prefix (genesis at position 0)
  [15, 16, 17, 18, 19, 20]  -- OOO section: parent (height 14) not yet known
  [6, 7, 8, 9, 10, 11, 12, 13, 14]  -- in-order suffix

Genesis is written first so that LoadGenesisBlock() (called during -reindex
startup) writes genesis at position 0 without corrupting any other block.

During reindex:
  - Heights 0-5 are processed in order.
  - Heights 15-20 are queued in mapBlocksUnknownParent (parent 14 unknown).
  - Heights 6-14 are processed in order.
  - When block 14 is accepted, block 15 is released recursively.
  - The recursive AcceptBlock for block 15 uses pxfieldHistory (temp map),
    so the key[2]->key[3] change from block 15 is visible when verifying
    block 16's proof.

Without the fix, block 16 would fail with 'bad-proof' because the global
CXFieldHistory has not yet recorded the key change introduced by block 15.
"""

import os
import struct
import time

from test_framework.blocktools import create_block, create_coinbase, createTestGenesisBlock
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, bytes_to_hex_str, NetworkDirName, MagicBytes, wait_until


class ReindexOutOfOrderFederationTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.sig_scheme = 0

        # Four aggregate key pairs.  key[0] matches the framework default so
        # the genesis block written by the test framework is valid.
        self.aggpubkeys = [
            "025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
            "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc",
            "02bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf",
            "03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b",
        ]
        self.aggprivkey = [
            "67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37",
            "dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b",
            "aa2c70c4b85a09be514292d04b27bbb0cc3f86d306d58fe87743d10a095ada07",
            "3087d8decc5f951f19a442397cf1eba1e2b064e68650c346502780b56454c6e2",
        ]
        self.aggprivkey_wif = [
            "cR4F4fGuKjDWxiYDtGtyM77WkrVhTgokVyM2ERxoxp7R4RQP9dgE",
            "cUwpWhH9CbYwjUWzfz1UVaSjSQm9ALXWRqeFFiZKnn8cV6wqNXQA",
            "cTHVmjaAwKtU75t89fg42SLx43nRxhsri6YY1Eynvs1V1tPRCfae",
            "cPD3D5AvmXhw7NGxQeaRhTVNW2UoYeibQAMhye7jzyM4ETG9d1ez",
        ]

        # Genesis uses key[0] — identical to the BitcoinTestFramework default.
        self.genesisBlock = createTestGenesisBlock(
            self.aggpubkeys[0], self.aggprivkey[0], int(time.time()) - 100)

    def _submit_federation_block(self, node, new_aggpubkey, sign_privkey, height):
        """Build, sign and submit a block that introduces a new aggregate pubkey."""
        tip = node.getbestblockhash()
        best = node.getblock(tip)
        blk = create_block(int(tip, 16), create_coinbase(height),
                           best["time"] + 1, new_aggpubkey)
        blk.solve(sign_privkey)
        result = node.submitblock(bytes_to_hex_str(blk.serialize()))
        assert result in (None, ""), "submitblock failed: %s" % result
        assert_equal(node.getbestblockhash(), blk.hash)

    def run_test(self):
        node = self.nodes[0]

        # ── Phase 1: build the chain ───────────────────────────────────────
        self.log.info("Building chain with multiple federation key changes")

        # Heights 1-4: regular blocks, key[0]
        node.generate(4, self.aggprivkey_wif[0])

        # Height 5: introduce key[1]
        self._submit_federation_block(node, self.aggpubkeys[1], self.aggprivkey[0], 5)

        # Heights 6-9: regular blocks, key[1]
        node.generate(4, self.aggprivkey_wif[1])

        # Height 10: introduce key[2]
        self._submit_federation_block(node, self.aggpubkeys[2], self.aggprivkey[1], 10)

        # Heights 11-14: regular blocks, key[2]
        node.generate(4, self.aggprivkey_wif[2])

        # Height 15: introduce key[3]  — this block will be in the OOO section
        self._submit_federation_block(node, self.aggpubkeys[3], self.aggprivkey[2], 15)

        # Heights 16-20: regular blocks, key[3]
        node.generate(5, self.aggprivkey_wif[3])

        tip_height = node.getblockcount()
        assert_equal(tip_height, 20)

        # ── Phase 2: collect raw block bytes before stopping ───────────────
        self.log.info("Collecting raw blocks for out-of-order blk file")

        # OOO section: heights OOO_START..tip appear before heights
        # EARLY_SPLIT+1..OOO_START-1 in the blk file.  Heights 0..EARLY_SPLIT
        # are kept at the very beginning so that genesis stays at file offset 0.
        EARLY_SPLIT = 5   # heights 0-5  appear first, in order
        OOO_START   = 15  # heights 15-20 come next  (out of order)
                          # heights 6-14 come last   (complete the chain)

        magic = MagicBytes()
        raw = {}
        for h in range(tip_height + 1):
            raw[h] = bytes.fromhex(node.getblock(node.getblockhash(h), 0))

        blk_file = os.path.join(node.datadir, NetworkDirName(), 'blocks', 'blk00000.dat')

        # Stop the node before touching its data directory.
        self.stop_nodes()

        # ── Phase 3: write out-of-order blk file ──────────────────────────
        # File layout:
        #   [0..EARLY_SPLIT]          in-order prefix  (genesis at byte 0)
        #   [OOO_START..tip]          OOO section      (parent height 14 unknown)
        #   [EARLY_SPLIT+1..OOO_START-1]  in-order suffix
        self.log.info(
            "Writing blk file: [0-%d] then OOO [%d-%d] then [%d-%d]"
            % (EARLY_SPLIT, OOO_START, tip_height, EARLY_SPLIT + 1, OOO_START - 1))
        write_order = (list(range(EARLY_SPLIT + 1))
                       + list(range(OOO_START, tip_height + 1))
                       + list(range(EARLY_SPLIT + 1, OOO_START)))
        with open(blk_file, 'wb') as f:
            for h in write_order:
                f.write(magic)
                f.write(struct.pack('<I', len(raw[h])))
                f.write(raw[h])

        # ── Phase 4: reindex in isolation (single node, no peers) ─────────
        self.log.info("Starting node with -reindex (isolated, no peers)")
        self.start_nodes([["-reindex"]])

        wait_until(lambda: node.getblockcount() >= tip_height, timeout=60)

        # ── Phase 5: verify ────────────────────────────────────────────────
        self.log.info("Check reindex with out-of-order blocks correctly applied "
                      "all federation key changes including the one inside the OOO section")
        info = node.getblockchaininfo()
        assert_equal(info["blocks"], tip_height)
        # aggregatePubkeys reports the height from which each key takes effect.
        expected_keys = [
            {self.aggpubkeys[0]: 0},
            {self.aggpubkeys[1]: 6},
            {self.aggpubkeys[2]: 11},
            {self.aggpubkeys[3]: 16},
        ]
        assert_equal(info["aggregatePubkeys"], expected_keys)


if __name__ == '__main__':
    ReindexOutOfOrderFederationTest().main()
