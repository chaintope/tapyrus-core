#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that the wallet can send and receive using all combinations of address types.

There is 1 nodes-under-test:
    - node0 uses legacy addresses

node5 exists to generate new blocks.

## Multisig address test

Test that adding a multisig address with:
    - an uncompressed pubkey always gives a legacy address
    - only compressed pubkeys gives the an `-addresstype` address

## Sending to address types test

A series of tests, iterating over node0-node4. In each iteration of the test, one node sends:
    - 10/101th of its balance to itself (using getrawchangeaddress for single key addresses)
    - 20/101th to the next node

Iterate over each node for single key addresses, and then over each node for
multisig addresses.


As every node sends coins after receiving, this also
verifies that spending coins sent to all these address types works.

## Change type test

Test that the nodes generate the correct change address type:
    - node0 always uses a legacy change address.
"""

from decimal import Decimal
import itertools

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    sync_blocks,
    sync_mempools,
    assert_raises_rpc_error
)

class AddressTypeTest(BitcoinTestFramework):
    colorid = "c11863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262"
    def set_test_params(self):
        self.num_nodes = 3

    def get_balances(self, confirmed=True):
        """Return a list of confirmed or unconfirmed balances."""
        if confirmed:
            return [self.nodes[i].getbalance() for i in [0,1]]
        else:
            return [self.nodes[i].getunconfirmedbalance() for i in [0,1]]

    def test_address(self, node, address, multisig, color=False):
        """Run sanity checks on an address."""
        info = self.nodes[node].getaddressinfo(address)
        assert(self.nodes[node].validateaddress(address)['isvalid'])
        if not multisig:
            # P2PKH
            assert(not info['isscript'])
            assert(not info['iswitness'])
            if color:
                assert(info['istoken'])
                assert_equal(info['color'], self.colorid)
            else:
                assert(not info['istoken'])
                assert('pubkey' in info)
        else:
            # P2SH-multisig
            assert(info['isscript'])
            assert_equal(info['script'], 'multisig')
            assert(not info['iswitness'])
            assert(not info['istoken'])
            assert('pubkeys' in info)


    def test_change_output_type(self, node_sender, destinations):
        txid = self.nodes[node_sender].sendmany(amounts=dict.fromkeys(destinations, 0.001))
        raw_tx = self.nodes[node_sender].getrawtransaction(txid)
        tx = self.nodes[node_sender].decoderawtransaction(raw_tx)

        # Make sure the transaction has change:
        assert_equal(len(tx["vout"]), len(destinations) + 1)

        # Make sure the destinations are included, and remove them:
        output_addresses = [vout['scriptPubKey']['addresses'][0] for vout in tx["vout"]]
        change_addresses = [d for d in output_addresses if d not in destinations]
        assert_equal(len(change_addresses), 1)

        self.test_address(node_sender, change_addresses[0], multisig=False)

    def run_test(self):
        # Mine 1 block on node5 to bring nodes out of IBD and make sure that
        # no coinbases are maturing for the nodes-under-test during the test
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        sync_blocks(self.nodes)

        uncompressed_1 = "0496b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a604f8141781e62294721166bf621e73a82cbf2342c858ee"
        uncompressed_2 = "047211a824f55b505228e4c3d5194c1fcfaa15a456abdf37f9b9d97a4040afc073dee6c89064984f03385237d92167c13e236446b417ab79a0fcae412ae3316b77"
        compressed_1 = "0296b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52"
        compressed_2 = "037211a824f55b505228e4c3d5194c1fcfaa15a456abdf37f9b9d97a4040afc073"

        # all addmultisigaddress are legacy.
        self.test_address(0, self.nodes[0].addmultisigaddress(2, [uncompressed_1, uncompressed_2])['address'], True, False)
        self.test_address(0, self.nodes[0].addmultisigaddress(2, [compressed_1, uncompressed_2])['address'], True, False)
        self.test_address(0, self.nodes[0].addmultisigaddress(2, [uncompressed_1, compressed_2])['address'], True, False)
        self.test_address(0, self.nodes[0].addmultisigaddress(2, [compressed_1, compressed_2])['address'], True, False)

        for  multisig,from_node  in itertools.product([False, True], [0,1]):

            self.log.info("Sending from node {} with{} multisig".format(from_node,  "" if multisig else "out"))
            old_balances = self.get_balances()
            self.log.debug("Old balances are {}".format(old_balances))
            to_send = (old_balances[from_node] / 101).quantize(Decimal("0.00000001"))
            sends = {}

            self.log.debug("Prepare sends")
            for n, to_node in enumerate([0,1]):
                if not multisig:
                    if from_node == to_node:
                        # When sending non-multisig to self, use getrawchangeaddress
                        address = self.nodes[to_node].getrawchangeaddress()
                        coloraddress = self.nodes[to_node].getrawchangeaddress(self.colorid)
                    else:
                        address = self.nodes[to_node].getnewaddress()
                        coloraddress = self.nodes[to_node].getnewaddress("", self.colorid)
                    #color address validation
                    self.test_address(to_node, coloraddress, False, True)

                else:
                    addr1 = self.nodes[to_node].getnewaddress()
                    addr2 = self.nodes[to_node].getnewaddress()
                    address = self.nodes[to_node].addmultisigaddress(2, [addr1, addr2])['address']

                #legacy address validation
                self.test_address(to_node, address, multisig)

                # Output entry
                sends[address] = to_send * 10 * (1 + n)

            self.log.debug("Sending: {}".format(sends))
            self.nodes[from_node].sendmany(sends)
            sync_mempools(self.nodes)

            unconf_balances = self.get_balances(False)
            self.log.debug("Check unconfirmed balances: {}".format(unconf_balances))
            assert_equal(unconf_balances[from_node], 0)
            if from_node != to_node:
                assert_equal(unconf_balances[to_node], to_send * 10 * (1 + n))

            # node1 collects fee and block subsidy to keep accounting simple
            self.nodes[2].generate(1, self.signblockprivkey_wif)
            sync_blocks(self.nodes)

            new_balances = self.get_balances()
            self.log.debug("Check new balances: {}".format(new_balances))
            # We don't know what fee was set, so we can only check bounds on the balance of the sending node
            if from_node != to_node:
                assert_greater_than(new_balances[from_node], old_balances[from_node] - ( to_send * 10 * 2 * (1 + n)))
                assert_greater_than(old_balances[from_node]  + to_send * 10 * 2 * (1 + n), new_balances[from_node])
                assert_equal(new_balances[to_node], old_balances[to_node] + to_send * 10 * ( 1+ n))

        # Get one address from node2
        to_address= self.nodes[2].getnewaddress()

        self.nodes[2].sendtoaddress(self.nodes[1].getnewaddress(), Decimal("1"))
        self.nodes[2].generate(1, self.signblockprivkey_wif)
        sync_blocks(self.nodes)
        assert_equal(self.nodes[1].getbalance(), new_balances[1] + 1)

        self.test_change_output_type(0, [to_address])

        assert_raises_rpc_error(-8, "Invalid color parameter", self.nodes[1].getnewaddress, '', 'c427282888')
        assert_raises_rpc_error(-8, "Invalid color parameter", self.nodes[1].getnewaddress, 'c0', 'c027282888')
        assert_raises_rpc_error(-8, "Invalid color parameter", self.nodes[1].getnewaddress, '', '27282888')
        assert_raises_rpc_error(-1, "end of data", self.nodes[1].getnewaddress, '', 'c127282888')
        assert_raises_rpc_error(-1, "end of data", self.nodes[1].getnewaddress, '', 'c10100')

        assert_raises_rpc_error(-8, "Invalid color parameter", self.nodes[1].getrawchangeaddress, 'c427282888')
        assert_raises_rpc_error(-8, "Invalid color parameter", self.nodes[1].getrawchangeaddress, 'c027282888')
        assert_raises_rpc_error(-8, "Invalid color parameter", self.nodes[1].getrawchangeaddress, '27282888')
        assert_raises_rpc_error(-1, "end of data", self.nodes[1].getrawchangeaddress, 'c127282888')
        assert_raises_rpc_error(-1, "end of data", self.nodes[1].getrawchangeaddress, 'c10100')

if __name__ == '__main__':
    AddressTypeTest().main()
