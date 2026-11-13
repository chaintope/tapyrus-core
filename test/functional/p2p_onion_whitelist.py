#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that Tor inbound peers are never whitelisted (CVE-2025-62860).

Peers on an onion bind (-bind=...=onion) skip -whitelist ranges, while the
same address on a clearnet bind is still whitelisted. The node refuses to
start when a -whitelist range covers an address Tor connects from.

Also covers the default and explicit onion binds, the warnings for binds
that leave Tor peers untagged or clearnet peers without a listener, and
the start-up errors for an onion bind that can't get a port of its own.
"""

import os
import socket

from test_framework.mininode import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import assert_equal, p2p_port, tor_port


class OnionWhitelistTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        onion_port = tor_port(0)
        onion_bind = "-bind=127.0.0.1:{}=onion".format(onion_port)

        self.log.info("A clearnet peer from a whitelisted range is whitelisted, a Tor peer is not")
        self.restart_node(0, extra_args=["-whitelist=127.0.0.1", onion_bind])
        node.add_p2p_connection(P2PInterface(node.time_to_connect))
        node.add_p2p_connection(P2PInterface(node.time_to_connect), dstport=onion_port)
        whitelisted = {peer["addrbind"]: peer["whitelisted"] for peer in node.getpeerinfo()}
        assert_equal(whitelisted, {
            "127.0.0.1:{}".format(p2p_port(0)): True,
            "127.0.0.1:{}".format(onion_port): False,
        })
        node.disconnect_p2ps()

        self.log.info("-whitebind still whitelists its own address")
        self.restart_node(0, extra_args=["-whitebind=127.0.0.1:{}".format(onion_port + 1), onion_bind])
        node.add_p2p_connection(P2PInterface(node.time_to_connect), dstport=onion_port + 1)
        assert_equal([peer["whitelisted"] for peer in node.getpeerinfo()], [True])
        node.disconnect_p2ps()
        self.stop_node(0)

        self.log.info("Refuse a -whitelist range covering loopback while -listenonion is on")
        node.assert_start_raises_init_error(
            ["-listenonion=1", "-torcontrol=127.0.0.1:1", "-whitelist=127.0.0.0/8"],
            r"Error: -whitelist=127\.0\.0\.0/8 covers 127\.0\.0\.1, an address Tor inbound peers can connect from",
            match=ErrorMatch.PARTIAL_REGEX)
        node.assert_start_raises_init_error(
            ["-listenonion=1", "-torcontrol=127.0.0.1:1", "-whitelist=::1"],
            r"Error: -whitelist=::1/128 covers ::1,",
            match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Refuse a -whitelist range covering the -torcontrol address")
        node.assert_start_raises_init_error(
            ["-listenonion=1", "-torcontrol=10.1.2.3:9051", "-whitelist=10.1.0.0/16"],
            r"Error: -whitelist=10\.1\.0\.0/16 covers 10\.1\.2\.3,",
            match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("Refuse a -whitelist range covering the -proxy or -onion address")
        # -proxy turns listening off unless -listen is given.
        node.assert_start_raises_init_error(
            ["-proxy=172.17.0.2:9050", "-listen=1", "-whitelist=172.17.0.0/16"],
            r"Error: -whitelist=172\.17\.0\.0/16 covers 172\.17\.0\.2,",
            match=ErrorMatch.PARTIAL_REGEX)
        node.assert_start_raises_init_error(
            ["-onion=172.18.0.2:9050", "-whitelist=172.18.0.2"],
            r"Error: -whitelist=172\.18\.0\.2/32 covers 172\.18\.0\.2,",
            match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("No check when the node does not listen")
        self.start_node(0, extra_args=["-listen=0", "-proxy=172.17.0.2:9050", "-whitelist=172.17.0.0/16"])
        self.stop_node(0)

        self.log.info("Reject an unknown -bind network suffix")
        node.assert_start_raises_init_error(
            ["-bind=127.0.0.1:{}=tor".format(onion_port)],
            "Error: Cannot resolve -bind address: '127.0.0.1:{}=tor'".format(onion_port))

        self.run_bind_scenarios(node, onion_port)

    def peers_whitelisted(self, node, ports):
        """Connect to each port and return {addrbind: whitelisted}."""
        for port in ports:
            node.add_p2p_connection(P2PInterface(node.time_to_connect), dstport=port)
        whitelisted = {peer["addrbind"]: peer["whitelisted"] for peer in node.getpeerinfo()}
        node.disconnect_p2ps()
        return whitelisted

    def run_bind_scenarios(self, node, onion_port):
        # Use tapyrusd's own default binds: drop the test config's bind=127.0.0.1 and the framework's binds.
        conf_path = os.path.join(node.datadir, "tapyrus.conf")
        with open(conf_path, encoding="utf8") as conf:
            lines = [line for line in conf if not line.startswith("bind=")]
        with open(conf_path, "w", encoding="utf8") as conf:
            conf.writelines(lines)
        node.add_test_binds = False
        port = p2p_port(0)
        # The P2P port + 1 is the next node's P2P port, which this one-node test leaves free.
        default_onion_port = port + 1
        tor_args = ["-listenonion=1", "-torcontrol=127.0.0.1:1"]
        forward_warning = ("Warning: No onion bind is given, so the Tor onion service forwards to the clearnet listener 0.0.0.0:{} "
                           "and its peers are not tagged as Tor. Add -bind=<addr>=onion to listen for them separately.").format(port)
        no_clearnet_warning = ("Warning: Every -bind is an onion bind, so clearnet peers can connect only to a -whitebind. "
                               "Add -bind=<addr> to listen for them.")

        self.log.info("No -bind: clearnet on the wildcard addresses, Tor on 127.0.0.1 at the P2P port + 1")
        self.start_node(0, extra_args=["-whitelist=127.0.0.1"])
        assert_equal(self.peers_whitelisted(node, [port, default_onion_port]), {
            "127.0.0.1:{}".format(port): True,
            "127.0.0.1:{}".format(default_onion_port): False,
        })
        self.stop_node(0)
        self.start_node(0, extra_args=tor_args)
        self.stop_node(0)

        self.log.info("A clearnet bind and an onion bind with its own port")
        self.start_node(0, extra_args=["-whitelist=127.0.0.1", "-bind=0.0.0.0", "-bind=127.0.0.1:{}=onion".format(onion_port)] + tor_args[1:])
        assert_equal(self.peers_whitelisted(node, [port, onion_port]), {
            "127.0.0.1:{}".format(port): True,
            "127.0.0.1:{}".format(onion_port): False,
        })
        self.stop_node(0)
        self.start_node(0, extra_args=tor_args + ["-bind=0.0.0.0", "-bind=127.0.0.1:{}=onion".format(onion_port)])
        self.stop_node(0)

        self.log.info("An onion bind without a port uses the P2P port + 1")
        self.start_node(0, extra_args=["-whitelist=127.0.0.1", "-bind=0.0.0.0", "-bind=127.0.0.1=onion"])
        assert_equal(self.peers_whitelisted(node, [default_onion_port]), {
            "127.0.0.1:{}".format(default_onion_port): False,
        })
        self.stop_node(0)

        self.log.info("Warn when every -bind is an onion bind, a -whitebind does not count")
        self.start_node(0, extra_args=["-bind=127.0.0.1:{}=onion".format(onion_port)])
        self.stop_node(0, expected_stderr=no_clearnet_warning)
        self.start_node(0, extra_args=["-whitebind=127.0.0.1:{}".format(onion_port + 1), "-bind=127.0.0.1:{}=onion".format(onion_port)])
        self.stop_node(0, expected_stderr=no_clearnet_warning)

        self.log.info("Warn when the onion service forwards to a clearnet bind")
        self.start_node(0, extra_args=tor_args + ["-bind=0.0.0.0"])
        self.stop_node(0, expected_stderr=forward_warning)
        self.start_node(0, extra_args=["-bind=0.0.0.0"])
        self.stop_node(0)

        self.log.info("Refuse an onion bind on the port of a clearnet bind")
        node.assert_start_raises_init_error(
            ["-bind=0.0.0.0", "-bind=127.0.0.1:{}=onion".format(port)],
            "Error: The onion bind 127.0.0.1:{0} shares its port with the clearnet bind 0.0.0.0:{0}. Give the onion bind its own port.".format(port))
        node.assert_start_raises_init_error(
            ["-bind=0.0.0.0", "-whitebind=127.0.0.1:{}".format(onion_port), "-bind=127.0.0.1:{}=onion".format(onion_port)],
            "Error: The onion bind 127.0.0.1:{0} shares its port with the clearnet bind 127.0.0.1:{0}. Give the onion bind its own port.".format(onion_port))

        self.log.info("Refuse -port=65535 when an onion bind needs the P2P port + 1")
        no_onion_port = "Error: -port=65535 leaves no port for an onion bind at the P2P port + 1. Give the onion bind its own port with -bind=<addr>:<port>=onion."
        node.assert_start_raises_init_error(["-port=65535"], no_onion_port)
        node.assert_start_raises_init_error(["-port=65535", "-bind=0.0.0.0", "-bind=127.0.0.1=onion"], no_onion_port)
        self.start_node(0, extra_args=["-port=65535", "-listen=0"])
        self.stop_node(0)

        self.log.info("Refuse to start when Tor's forwarding target cannot be bound")
        bind_error = "Error: Unable to bind to 127.0.0.1:{} on this computer. Tapyrus Core is probably already running.".format(onion_port)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as squatter:
            squatter.bind(("127.0.0.1", onion_port))
            squatter.listen()
            node.assert_start_raises_init_error(
                tor_args + ["-bind=0.0.0.0", "-bind=127.0.0.1:{}=onion".format(onion_port)],
                r"Error: Failed to listen on 127\.0\.0\.1:{}, where Tor forwards the onion service\.".format(onion_port),
                match=ErrorMatch.PARTIAL_REGEX)
            # Without -listenonion, nothing forwards there, so the node runs on its clearnet bind.
            self.start_node(0, extra_args=["-bind=0.0.0.0", "-bind=127.0.0.1:{}=onion".format(onion_port)])
            self.stop_node(0, expected_stderr=bind_error)


if __name__ == '__main__':
    OnionWhitelistTest().main()
