#!/usr/bin/env python3

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes
from test_framework.messages import (CBlock)

import io

class SignedBlocksTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def run_test(self):
        # Start with a 200 block chain
        assert_equal(self.nodes[0].getblockcount(), 200)

        address = self.nodes[0].getnewaddress()

        block_str = self.nodes[0].getnewblock(address)
        block = CBlock()
        block.deserialize(io.BytesIO(hex_str_to_bytes(block_str)))

        # check candidate block
        assert_equal(block.hashPrevBlock, int(self.nodes[0].getbestblockhash(), 16))
        assert_equal(len(block.proof), 0)

if __name__ == '__main__':
    SignedBlocksTest().main()