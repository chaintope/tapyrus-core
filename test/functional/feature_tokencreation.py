#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2021 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test colored coin issue using create_colored_transaction python API in tapyrus wallet."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.blocktools import findTPC, create_colored_transaction, TOKEN_TYPES

class TokenAPITest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True

    def run_test(self):

        self.log.info("Cresting blocks")
        self.nodes[0].generate(2, self.signblockprivkey_wif)
        self.sync_all()
        self.nodes[1].generate(2, self.signblockprivkey_wif)
        self.sync_all()
        self.nodes[2].generate(2, self.signblockprivkey_wif)
        self.sync_all()

        self.log.info("Testing token issue using create_colored_transaction ")
        new_color1 = create_colored_transaction(1, 1000, self.nodes[0])['color']
        new_color2 = create_colored_transaction(2, 1000, self.nodes[1])['color']
        new_color3 = create_colored_transaction(3, 1, self.nodes[2])['color']

        self.sync_all()
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color1], 1000)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color2], 1000)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color3], 1)

        self.log.info("Testing token transfer using create_colored_transaction ")
        create_colored_transaction(1, 500, self.nodes[0], False, new_color1, self.nodes[1])
        create_colored_transaction(2, 500, self.nodes[1], False, new_color2, self.nodes[2])
        create_colored_transaction(3, 1, self.nodes[2], False, new_color3, self.nodes[0])

        self.sync_all()
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        walletinfo = self.nodes[0].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color1], 500)
        assert_equal(walletinfo['balance'][new_color3], 1)

        walletinfo = self.nodes[1].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color2], 500)
        assert_equal(walletinfo['balance'][new_color1], 500)

        walletinfo = self.nodes[2].getwalletinfo()
        assert_equal(walletinfo['balance'][new_color3], 0)
        assert_equal(walletinfo['balance'][new_color2], 500)


if __name__ == '__main__':
    TokenAPITest().main()