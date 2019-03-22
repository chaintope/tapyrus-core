#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test logic for setting nMinimumChainWork on command line.
basic chain setup

"""

import time

from test_framework.test_framework import BitcoinTestFramework


class NodeSynchTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 4
        #self.setup_clean_chain = True
        
    def run_test(self):
        # Because nodes in regtest are all manual connections (eg using
        # addnode), node1 should not have disconnected node0. If not for that,
        # we'd expect node1 to have disconnected node0 for serving an
        # insufficient work chain, in which case we'd need to reconnect them to
        # continue the test.
        self.log.info("Blockcounts: %s", [n.getblockcount() for n in self.nodes])
        self.sync_all()
        self.log.info("Blockcounts: %s", [n.getblockcount() for n in self.nodes])

if __name__ == '__main__':
    NodeSynchTest().main()
