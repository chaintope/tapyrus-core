#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test getheaders sent in response to block announced in INV

Test that a block announcement using INV results in each peer receiving a getheaders message with its best block.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.messages import (
    CInv,
    MSG_BLOCK,
    msg_headers,
    msg_inv,
)
from test_framework.mininode import (
    mininode_lock,
    P2PInterface
)
from test_framework.util import (
    assert_equal,
    wait_until
)
import random

class HeadersSyncTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def announce_random_block(self, peers):
        last_block = random.randrange(1<<256)
        new_block_announcement = msg_inv(inv=[CInv(MSG_BLOCK, random.randrange(1<<256)), CInv(MSG_BLOCK, last_block)])
        for p in peers:
            p.send_and_ping(new_block_announcement)
        return last_block

    def run_test(self):
        self.log.info("Adding a peer to node0")
        peer1 = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))
        best_block_hash = int(self.nodes[0].getbestblockhash(), 16)

        # Wait for peer1 to receive a getheaders
        # If no block hash is provided, checks whether any getheaders message has been received by the node."""
        def test_function():
            last_getheaders = peer1.last_message.pop("getheaders", None)
            if last_getheaders is None:
                return False
            return best_block_hash == last_getheaders.locator.vHave[0]

        wait_until(test_function, timeout=10)
        # An empty reply will clear the outstanding getheaders request,
        # allowing additional getheaders requests to be sent to this peer in
        # the future.
        peer1.send_message(msg_headers())

        self.log.info("Connecting two more peers to node0")
        # Connect 2 more peers; they should not receive a getheaders yet
        peer2 = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))
        peer3 = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))

        all_peers = [peer1, peer2, peer3]

        self.log.info("Verify that peer2 and peer3 don't receive a getheaders after connecting")
        for p in all_peers:
            p.sync_with_ping()
        with mininode_lock:
            assert "getheaders" in peer2.last_message
            assert "getheaders" in peer3.last_message

        self.log.info("Have all peers announce a new block")
        last_block = self.announce_random_block(all_peers)

        self.log.info("Check that peer1 receives a getheaders in response")
        best_block_hash = int(self.nodes[0].getbestblockhash(), 16)
        wait_until(test_function, timeout=10)
        peer1.send_message(msg_headers()) # Send empty response, see above

        self.log.info("Check that peer2 and peer3 received a getheaders in response")
        count = 0
        peer_receiving_getheaders = None
        for p in [peer2, peer3]:
            with mininode_lock:
                if "getheaders" in p.last_message:
                    count += 1
                    peer_receiving_getheaders = p
                    p.send_message(msg_headers()) # Send empty response, see above

        assert_equal(count, 2)

        self.log.info("Announce another new block, from all peers")
        last_block = self.announce_random_block(all_peers)

        self.log.info("Check that peer1 receives a getheaders in response")
        best_block_hash = int(self.nodes[0].getbestblockhash(), 16)
        wait_until(test_function, timeout=10)

        self.log.info("Success!")

if __name__ == '__main__':
    HeadersSyncTest().main()

