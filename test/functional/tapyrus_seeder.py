#!/usr/bin/env python3
# Copyright (c) 2020 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the tapyrus seeder using this private network.

- setup a private network with 3 nodes.
- generate blocks at a consistent pace.

How to use this test:
1. run this script to setup a tapyrus-core network with networkid 2
2. run tapyrus-seeder in a terminal with networkid 2
3. measure the statistics for at least 2 hours and verify their consistency to the network

"""

from test_framework.test_framework import BitcoinTestFramework, initialize_datadir
from test_framework.blocktools import createTestGenesisBlock
from test_framework.util import assert_equal
import time

class TapyrusSeederTest (BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = True
        self.networkid = 2
        self.extra_args =  [["-networkid=2"], ["-networkid=2"], ["-networkid=2"], ["-networkid=2"]]
        self.genesisBlock = createTestGenesisBlock(self.signblockpubkey, self.signblockprivkey)


    def run_test (self):
        for i in range(4):
            initialize_datadir(self.options.tmpdir, i)
            self.writeGenesisBlockToFile(self.nodes[i].datadir, networkid=2)

        self.nodes[0].generate(101, self.signblockprivkey)
        self.sync_all([self.nodes])
        self.log.info("Network is at block height 101")

        start_time = time.time()
        current_time = start_time
        tips = self.nodes[0].getchaintips ()
        assert_equal (len (tips), 1)
        assert_equal (tips[0]['branchlen'], 0)
        assert_equal (tips[0]['height'], 101)
        assert_equal (tips[0]['status'], 'active')

        # run 24 hours
        self.log.info("Running for 24 hours")
        while(current_time - start_time < 86400):
            for i in range(4):
                self.nodes[i].generate(1, self.signblockprivkey)
                time.sleep(10)
                self.sync_all([self.nodes])
                current_time = time.time()


if __name__ == '__main__':
    TapyrusSeederTest ().main ()
