#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RPC calls related to net.

Tests correspond to code in rpc/net.cpp.
"""

import json
from test_framework.timeout_config import TAPYRUSD_MIN_TIMEOUT
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
    connect_nodes_bi,
    p2p_port,
    wait_until,
)

class NetTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def run_test(self):
        self._test_connection_count()
        self._test_getnettotals()
        self._test_getnetworkinginfo()
        self._test_getaddednodeinfo()
        self._test_getpeerinfo()

    def _test_connection_count(self):
        # connect_nodes_bi connects each node to the other
        assert_equal(self.nodes[0].getconnectioncount(), 2)

    def _test_getnettotals(self):
        # getnettotals totalbytesrecv and totalbytessent should be
        # consistent with getpeerinfo. Since the RPC calls are not atomic,
        # and messages might have been recvd or sent between RPC calls, call
        # getnettotals before and after and verify that the returned values
        # from getpeerinfo are bounded by those values.
        net_totals_before = self.nodes[0].getnettotals()
        peer_info = self.nodes[0].getpeerinfo()
        net_totals_after = self.nodes[0].getnettotals()
        assert_equal(len(peer_info), 2)
        peers_recv = sum([peer['bytesrecv'] for peer in peer_info])
        peers_sent = sum([peer['bytessent'] for peer in peer_info])

        assert_greater_than_or_equal(peers_recv, net_totals_before['totalbytesrecv'])
        assert_greater_than_or_equal(net_totals_after['totalbytesrecv'], peers_recv)
        assert_greater_than_or_equal(peers_sent, net_totals_before['totalbytessent'])
        assert_greater_than_or_equal(net_totals_after['totalbytessent'], peers_sent)

        # test getnettotals and getpeerinfo by doing a ping
        # the bytes sent/received should change
        # note ping and pong are 32 bytes each
        self.nodes[0].ping()
        wait_until(lambda: (self.nodes[0].getnettotals()['totalbytessent'] >= net_totals_after['totalbytessent'] + 32 * 2), timeout=TAPYRUSD_MIN_TIMEOUT)
        wait_until(lambda: (self.nodes[0].getnettotals()['totalbytesrecv'] >= net_totals_after['totalbytesrecv'] + 32 * 2), timeout=TAPYRUSD_MIN_TIMEOUT)

        peer_info_after_ping = self.nodes[0].getpeerinfo()
        for before, after in zip(peer_info, peer_info_after_ping):
            before_pong = before['bytesrecv_per_msg'].get('pong', 0)
            before_ping = before['bytessent_per_msg'].get('ping', 0)
            assert_greater_than_or_equal(after['bytesrecv_per_msg']['pong'], before_pong + 32)
            assert_greater_than_or_equal(after['bytessent_per_msg']['ping'], before_ping + 32)

    def _test_getnetworkinginfo(self):
        assert_equal(self.nodes[0].getnetworkinfo()['networkactive'], True)
        assert_equal(self.nodes[0].getnetworkinfo()['connections'], 2)

        self.nodes[0].setnetworkactive(False)
        assert_equal(self.nodes[0].getnetworkinfo()['networkactive'], False)
        # Wait a bit for all sockets to close
        wait_until(lambda: self.nodes[0].getnetworkinfo()['connections'] == 0, timeout=TAPYRUSD_MIN_TIMEOUT)

        self.nodes[0].setnetworkactive(True)
        connect_nodes_bi(self.nodes, 0, 1)
        assert_equal(self.nodes[0].getnetworkinfo()['networkactive'], True)
        assert_equal(self.nodes[0].getnetworkinfo()['connections'], 2)

    def _test_getaddednodeinfo(self):
        assert_equal(self.nodes[0].getaddednodeinfo(), [])
        # add a node (node2) to node0
        ip_port = "127.0.0.1:{}".format(p2p_port(2))
        self.nodes[0].addnode(ip_port, 'add')
        # check that the node has indeed been added
        added_nodes = self.nodes[0].getaddednodeinfo(ip_port)
        assert_equal(len(added_nodes), 1)
        assert_equal(added_nodes[0]['addednode'], ip_port)
        # check that a non-existent node returns an error
        assert_raises_rpc_error(-24, "Node has not been added", self.nodes[0].getaddednodeinfo, '1.1.1.1')

    def _test_getpeerinfo(self):
        # Track outbound connections from the beginning and match them with inbound connections
        # on the other peer. This is more reliable than trying to match all peers at once.
        # On slow hardware, getsockname() may fail initially, causing addrbind to be missing.

        # Step 1: Get the target peer addresses that each node is connecting to
        # Node 0 connects to Node 1's P2P port, and vice versa
        node0_target = f"127.0.0.1:{p2p_port(1)}"
        node1_target = f"127.0.0.1:{p2p_port(0)}"

        self.log.info(f"Node 0 should have outbound connection to: {node0_target}")
        self.log.info(f"Node 1 should have outbound connection to: {node1_target}")

        def find_stable_connections():
            peer_info = [x.getpeerinfo() for x in self.nodes]

            # Pretty print peer info for debugging
            self.log.info("=== Node 0 Peer Info ===")
            for i, peer in enumerate(peer_info[0]):
                self.log.info(f"Peer {i}:")
                self.log.info(json.dumps({
                    'id': peer.get('id'),
                    'addr': peer.get('addr'),
                    'addrbind': peer.get('addrbind'),
                    'inbound': peer.get('inbound'),
                    'bytesrecv': peer.get('bytesrecv'),
                    'bytessent': peer.get('bytessent')
                }, indent=2, default=str))

            self.log.info("=== Node 1 Peer Info ===")
            for i, peer in enumerate(peer_info[1]):
                self.log.info(f"Peer {i}:")
                self.log.info(json.dumps({
                    'id': peer.get('id'),
                    'addr': peer.get('addr'),
                    'addrbind': peer.get('addrbind'),
                    'inbound': peer.get('inbound'),
                    'bytesrecv': peer.get('bytesrecv'),
                    'bytessent': peer.get('bytessent')
                }, indent=2, default=str))

            # Check each node has exactly 2 peers
            if len(peer_info[0]) != 2 or len(peer_info[1]) != 2:
                self.log.info(f"Waiting for 2 peers on each node (currently: node0={len(peer_info[0])}, node1={len(peer_info[1])})")
                return False

            # Step 2: Find valid outbound connections (filter out any stale connections)
            # Outbound connections are identified by inbound=False and matching the target address
            node0_outbound = [p for p in peer_info[0] if not p.get('inbound') and p.get('addr') == node0_target]
            node1_outbound = [p for p in peer_info[1] if not p.get('inbound') and p.get('addr') == node1_target]

            self.log.info(f"Node 0 valid outbound connections to {node0_target}: {len(node0_outbound)}")
            self.log.info(f"Node 1 valid outbound connections to {node1_target}: {len(node1_outbound)}")

            if len(node0_outbound) == 0 or len(node1_outbound) == 0:
                self.log.info("Waiting for valid outbound connections...")
                return False

            # Step 3: For each valid outbound connection, find the matching inbound connection
            # on the other peer by matching addrbind
            for out_peer in node0_outbound:
                addrbind_out = out_peer.get('addrbind')
                if not addrbind_out:
                    self.log.info(f"Node 0 outbound peer {out_peer.get('id')} missing addrbind, waiting...")
                    continue

                # Find matching inbound connection on node1 where addr == our addrbind
                for in_peer in peer_info[1]:
                    if in_peer.get('inbound') and in_peer.get('addr') == addrbind_out:
                        # Verify the reverse direction
                        addrbind_in = in_peer.get('addrbind')
                        if addrbind_in == out_peer.get('addr'):
                            self.log.info(f"Found matching connection: Node0.outbound(id={out_peer.get('id')}) <-> Node1.inbound(id={in_peer.get('id')})")
                            self.log.info(f"Node0.outbound.addrbind={addrbind_out} == Node1.inbound.addr={in_peer.get('addr')}")
                            self.log.info(f"Node1.inbound.addrbind={addrbind_in} == Node0.outbound.addr={out_peer.get('addr')}")

                            # Also verify the other direction
                            for out_peer2 in node1_outbound:
                                addrbind_out2 = out_peer2.get('addrbind')
                                if not addrbind_out2:
                                    continue
                                for in_peer2 in peer_info[0]:
                                    if in_peer2.get('inbound') and in_peer2.get('addr') == addrbind_out2:
                                        addrbind_in2 = in_peer2.get('addrbind')
                                        if addrbind_in2 == out_peer2.get('addr'):
                                            self.log.info(f"Found matching connection: Node1.outbound(id={out_peer2.get('id')}) <-> Node0.inbound(id={in_peer2.get('id')})")
                                            self.log.info(f"Node1.outbound.addrbind={addrbind_out2} == Node0.inbound.addr={in_peer2.get('addr')}")
                                            self.log.info(f"Node0.inbound.addrbind={addrbind_in2} == Node1.outbound.addr={out_peer2.get('addr')}")
                                            self.log.info("Both bidirectional connections verified!")
                                            return True

            # Count how many peers have addrbind populated
            node0_with_addrbind = sum(1 for peer in peer_info[0] if peer.get('addrbind'))
            node1_with_addrbind = sum(1 for peer in peer_info[1] if peer.get('addrbind'))
            self.log.info(f"Node0: {node0_with_addrbind}/{len(peer_info[0])} peers with addrbind, Node1: {node1_with_addrbind}/{len(peer_info[1])} peers with addrbind")
            self.log.info("Waiting for addrbind to be populated on all connections...")
            return False

        wait_until(find_stable_connections, timeout=TAPYRUSD_MIN_TIMEOUT)

if __name__ == '__main__':
    NetTest().main()
