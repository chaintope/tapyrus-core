#!/usr/bin/env python3
# Copyright (c) 2020-2021 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test addr relay
"""

import random
import time

from test_framework.messages import (
    CAddress,
    msg_addr,
    msg_getaddr,
    msg_verack,
    msg_headers,
    NODE_NETWORK
)
from test_framework.mininode import (
    P2PInterface,
    mininode_lock,

)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    wait_until
)
#from test_framework.test_node import assert_equal

ONE_MINUTE  = 60
TEN_MINUTES = 10 * ONE_MINUTE
ONE_HOUR    = 60 * ONE_MINUTE
TWO_HOURS   =  2 * ONE_HOUR
ONE_DAY     = 24 * ONE_HOUR

class AddrReceiver(P2PInterface):
    num_ipv4_received = 0
    test_addr_contents = False
    _tokens = 1
    send_getaddr = True

    def __init__(self, time_to_connect, test_addr_contents=False, send_getaddr=True):
        super().__init__(time_to_connect)
        self.test_addr_contents = test_addr_contents
        self.send_getaddr = send_getaddr

    def on_addr(self, message):
        for addr in message.addrs:
            self.num_ipv4_received += 1
            if self.test_addr_contents:
                # relay_tests checks the content of the addr messages match
                # expectations based on the message creation in setup_addr_msg
                assert_equal(addr.nServices, 1)
                if not 8333 <= addr.port < 8343:
                    raise AssertionError("Invalid addr.port of {} (8333-8342 expected)".format(addr.port))
                assert addr.ip.startswith('123.123.')

    def on_getaddr(self, message):
        # When the node sends us a getaddr, it increments the addr relay tokens for the connection by 1000
        self._tokens += 1000

    @property
    def tokens(self):
        with mininode_lock:
            return self._tokens

    def increment_tokens(self, n):
        # When we move mocktime forward, the node increments the addr relay tokens for its peers
        with mininode_lock:
            self._tokens += n

    def addr_received(self):
        return self.num_ipv4_received != 0

    def on_version(self, message):
        #self.send_version()
        self.send_message(msg_verack())
        if (self.send_getaddr):
            self.send_message(msg_getaddr())

    def getaddr_received(self):
        return self.message_count['getaddr'] > 0


class AddrTest(BitcoinTestFramework):
    counter = 0
    mocktime = int(time.time())

    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-whitelist=127.0.0.1"]]

    def run_test(self):
        self.oversized_addr_test()
        self.relay_tests()
        self.destination_rotates_once_in_24_hours_test()
        self.blocksonly_mode_tests()
        self.rate_limit_tests()

    def setup_addr_msg(self, num, sequential_ips=True):
        addrs = []
        for i in range(num):
            addr = CAddress()
            addr.time = self.mocktime + random.randrange(-100, 100)
            addr.nServices = NODE_NETWORK
            if sequential_ips:
                assert self.counter < 256 ** 2  # Don't allow the returned ip addresses to wrap.
                addr.ip = f"123.123.{self.counter // 256}.{self.counter % 256}"
                self.counter += 1
            else:
                addr.ip = f"{random.randrange(128,169)}.{random.randrange(1,255)}.{random.randrange(1,255)}.{random.randrange(1,255)}"
            addr.port = 8333 + i
            addrs.append(addr)

        msg = msg_addr()
        msg.addrs = addrs
        return msg

    def send_addr_msg(self, source, msg, receivers):
        source.send_and_ping(msg)
        # invoke m_next_addr_send timer:
        # `addr` messages are sent on an exponential distribution with mean interval of 30s.
        # Setting the mocktime 600s forward gives a probability of (1 - e^-(600/30)) that
        # the event will occur (i.e. this fails once in ~500 million repeats).
        self.mocktime += 10 * 60
        self.nodes[0].setmocktime(self.mocktime)
        for peer in receivers:
            peer.sync_with_ping()

    def oversized_addr_test(self):
        self.log.info('Send an addr message that is too large')
        addr_source = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))

        msg = self.setup_addr_msg(1010)
        for i in range(0,11):
            addr_source.send_message(msg)
            self.nodes[0].assert_debug_log(['Warning: not banning local peer'])

        self.nodes[0].disconnect_p2ps()

    def relay_tests(self):
        self.log.info('Test address relay')
        self.log.info('Check that addr message content is relayed and added to addrman')
        addr_source = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))
        num_receivers = 7
        receivers = []
        for _ in range(num_receivers):
            receivers.append(self.nodes[0].add_p2p_connection(AddrReceiver(self.nodes[0].time_to_connect, test_addr_contents=True)))

        # Keep this with length <= 10. Addresses from larger messages are not
        # relayed.
        num_ipv4_addrs = 10
        msg = self.setup_addr_msg(num_ipv4_addrs)
        self.send_addr_msg(addr_source, msg, receivers)

        # Every IPv4 address must be relayed to one/ two peers, other than the
        # originating node (addr_source).
        peerinfo = self.nodes[0].getpeerinfo()[0]
        addrs_processed = peerinfo['addr_processed']
        addrs_rate_limited = peerinfo['addr_rate_limited']
        self.log.debug(f"addrs_processed = {addrs_processed}, addrs_rate_limited = {addrs_rate_limited}")

        assert_equal(addrs_processed, 1)
        assert_equal(7, sum(r.tokens for r in receivers))
        assert_equal(addrs_rate_limited, 9)

        self.nodes[0].disconnect_p2ps()

    def sum_addr_messages(self, msgs_dict):
        return sum(bytes_received for (msg, bytes_received) in msgs_dict.items() if msg in ['addr', 'getaddr'])

    def blocksonly_mode_tests(self):
        self.log.info('Test addr relay in -blocksonly mode')
        self.restart_node(0, ["-blocksonly", "-whitelist=127.0.0.1"])
        self.mocktime = int(time.time())

        self.log.info('Check that we send getaddr messages')
        full_outbound_peer = self.nodes[0].add_p2p_connection(AddrReceiver(self.nodes[0].time_to_connect))
        full_outbound_peer.sync_with_ping()
        assert not full_outbound_peer.getaddr_received()

        self.log.info('Check that we relay address messages')
        addr_source = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))
        msg = self.setup_addr_msg(2)
        self.send_addr_msg(addr_source, msg, [full_outbound_peer])

        self.nodes[0].disconnect_p2ps()

    def send_addrs_and_test_rate_limiting(self, peer, new_addrs, total_addrs):
        """Send an addr message and check that the number of addresses processed and rate-limited is as expected"""

        peer.send_and_ping(self.setup_addr_msg(new_addrs, sequential_ips=False))

        peerinfo = self.nodes[0].getpeerinfo()[0]
        addrs_processed = peerinfo['addr_processed']
        addrs_rate_limited = peerinfo['addr_rate_limited']
        self.log.debug(f"addrs_processed = {addrs_processed}, addrs_rate_limited = {addrs_rate_limited}")

        assert_equal(addrs_processed, min(total_addrs, peer.tokens))
        assert_equal(addrs_rate_limited, max(0, total_addrs - peer.tokens))

    def rate_limit_tests(self):
        self.mocktime = int(time.time())
        self.restart_node(0, [])
        self.nodes[0].setmocktime(self.mocktime)

        self.log.info(f'Test rate limiting of addr processing for peers')
        peer = self.nodes[0].add_p2p_connection(AddrReceiver(self.nodes[0].time_to_connect))

        # Send 600 addresses.
        self.send_addrs_and_test_rate_limiting(peer, new_addrs=600, total_addrs=600)

        # Send 600 more addresses.
        self.send_addrs_and_test_rate_limiting(peer, new_addrs=600, total_addrs=1200)

        # Send 10 more. As we reached the processing limit for all nodes, no more addresses should be procesesd.
        self.send_addrs_and_test_rate_limiting(peer, new_addrs=10, total_addrs=1210)

        # Advance the time by 100 seconds, permitting the processing of 10 more addresses.
        # Send 200 and verify that 10 are processed.
        self.mocktime += 100
        self.nodes[0].setmocktime(self.mocktime)
        peer.increment_tokens(10)

        self.send_addrs_and_test_rate_limiting(peer, new_addrs=200, total_addrs=1410)

        # Advance the time by 1000 seconds, permitting the processing of 100 more addresses.
        # Send 200 and verify that 100 are processed.
        self.mocktime += 1000
        self.nodes[0].setmocktime(self.mocktime)
        peer.increment_tokens(100)

        self.send_addrs_and_test_rate_limiting(peer, new_addrs=200, total_addrs=1610)

        self.nodes[0].disconnect_p2ps()

    def get_nodes_that_received_addr(self, peer, receiver_peer, addr_receivers,
                                     time_interval_1, time_interval_2):

        # Clean addr response related to the initial getaddr. There is no way to avoid initial
        # getaddr because the peer won't self-announce then.
        for addr_receiver in addr_receivers:
            addr_receiver.num_ipv4_received = 0

        for _ in range(10):
            self.mocktime += time_interval_1
            self.msg.addrs[0].time = self.mocktime + TEN_MINUTES
            self.nodes[0].setmocktime(self.mocktime)
            self.nodes[0].assert_debug_log(['received: addr (31 bytes) peer=0'])
            peer.send_and_ping(self.msg)
            self.mocktime += time_interval_2
            self.nodes[0].setmocktime(self.mocktime)
            receiver_peer.sync_with_ping()
        return [node for node in addr_receivers if node.addr_received()]

    def destination_rotates_once_in_24_hours_test(self):
        self.restart_node(0, [])

        self.log.info('Test within 24 hours an addr relay destination is rotated at most once')
        self.mocktime = int(time.time())
        self.msg = self.setup_addr_msg(1)
        self.addr_receivers = []
        peer = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))
        receiver_peer = self.nodes[0].add_p2p_connection(AddrReceiver(self.nodes[0].time_to_connect))
        addr_receivers = [self.nodes[0].add_p2p_connection(AddrReceiver(self.nodes[0].time_to_connect)) for _ in range(20)]
        nodes_received_addr = self.get_nodes_that_received_addr(peer, receiver_peer, addr_receivers, 0, TWO_HOURS)  # 10 intervals of 2 hours
        # Per RelayAddress, we would announce these addrs to 1 0r 2 destinations per day.
        assert_greater_than_or_equal(2, len(nodes_received_addr))
        self.nodes[0].disconnect_p2ps()


if __name__ == '__main__':
    AddrTest().main()
