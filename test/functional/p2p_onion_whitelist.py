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

Against a fake Tor control port, checks where each combination of -proxy,
-listen, -bind and -listenonion asks Tor to forward the onion service, and
which listeners it opens.

Scenarios. P is the P2P port, "tagged" means the peer is on an onion bind and
skips -whitelist ranges. -listenonion is off unless given, as in the test
config. The test binds loopback; the last two columns describe the same
options on a reachable host with Tor running, as seen by an external node:
  Plain node: when it dials our IP:P / when we dial it.
  Tor node:   when it dials our .onion / when we dial its .onion.

| Scenario | Options | Use | Expected | Plain node: dials us / we dial it | Tor node: dials us / we dial it |
|---|---|---|---|---|---|
| Tor peers never whitelisted | -whitelist=127.0.0.1, clearnet bind, onion bind | Onion service set up in torrc next to trusted local peers | Clearnet peer in the range whitelisted, onion bind peer not | Accepted on the clearnet bind / sees our IP | Only through a torrc onion service, tagged / can't, no onion proxy |
| -whitebind still whitelists | -whitebind, onion bind | Trusted peers on a port of their own | Whitebind peer whitelisted | Accepted on the whitebind, whitelisted / sees our IP | Only through a torrc onion service, tagged / can't, no onion proxy |
| Whitelist covering a Tor address | -whitelist covering 127.0.0.1, ::1, -torcontrol, -proxy or -onion, while listening | Stop Tor peers being whitelisted | Start-up error | Node doesn't start | Node doesn't start |
| No check without listening | -listen=0 -proxy=A -whitelist covering A | Whitelist on a node that only dials out | Starts | Refused / sees a Tor exit IP | No onion service / sees us from 127.0.0.1 on its onion bind |
| Unknown -bind suffix | -bind=<addr>=tor | Typo | Start-up error | Node doesn't start | Node doesn't start |
| No -bind | none, or -listenonion=1 | Default node | Clearnet on 0.0.0.0 and :: at P, onion bind 127.0.0.1:P+1; Tor forwards to P+1 | Accepted on the clearnet bind / sees our IP | With -listenonion tagged on P+1, else no onion service / with -listenonion through the onion proxy set after Tor login, else can't |
| Clearnet and onion bind | -bind=<clearnet> -bind=127.0.0.1:<port>=onion | Node serving clearnet and Tor | Peers tagged by bind | Accepted on the clearnet bind / sees our IP | With -listenonion tagged, else no onion service / as for No -bind |
| Onion bind without a port | -bind=<clearnet> -bind=127.0.0.1=onion | Short form | Onion bind at P+1 | Accepted on the clearnet bind / sees our IP | As for No -bind |
| Every -bind is an onion bind | onion binds only, maybe a -whitebind | Tor-only inbound | Warning: clearnet peers can only use a -whitebind | Refused, or accepted on the whitebind / sees our IP | With -listenonion tagged, else no onion service / as for No -bind |
| Onion service to a clearnet bind | -listenonion=1 -bind=<clearnet> | Misconfigured Tor node | Warning; Tor forwards to the clearnet bind; -whitelist=127.0.0.1 refused | Accepted on the clearnet bind / sees our IP | Accepted on the clearnet bind from 127.0.0.1, not tagged / through the onion proxy set after Tor login |
| Onion bind sharing a port | onion bind on the port of a clearnet bind or -whitebind | Misconfiguration | Start-up error | Node doesn't start | Node doesn't start |
| -port=65535 | -port=65535 needing P+1 | Misconfiguration | Start-up error; starts with -listen=0 | Node doesn't start | Node doesn't start |
| Tor's target taken | -listenonion=1, onion bind port in use | Port clash | Start-up error; without -listenonion starts with a bind error | Node doesn't start | Node doesn't start |
| -proxy, clearnet and onion bind | -proxy -listenonion=1 -bind=<clearnet> -bind=<onion> | Public node whose outbound goes through Tor | Tor forwards to the onion bind; both listeners accept | Accepted on the clearnet bind / sees a Tor exit IP | Tagged on the onion bind / sees us from 127.0.0.1 on its onion bind |
| -proxy -listen=1 | -proxy -listen=1 -listenonion=1 | Tor node that also listens on clearnet | Listens on P and P+1; Tor forwards to P+1 | Accepted on P, which shows our IP runs a node / sees a Tor exit IP | Tagged on P+1 / sees us from 127.0.0.1 on its onion bind |
| Tor client | -proxy, or -proxy -listen=0 | Most private client | No listener, no Tor control connection | Refused / sees a Tor exit IP | No onion service / sees us from 127.0.0.1 on its onion bind |
| -listen=0 with binds | -proxy -listen=0 -bind=... | Misconfiguration | Start-up error | Node doesn't start | Node doesn't start |
| Onion service set up by hand | -listenonion=0 -bind=<clearnet> -bind=<onion> | Tor service in torrc | No Tor control connection; onion bind peers not whitelisted | Accepted on the clearnet bind / sees our IP | Through the torrc onion service, tagged / can't, no onion proxy |
| No proxy with -listenonion | -listenonion=1 -bind=<clearnet> -bind=<onion> | Tor only for inbound | Tor forwards to the onion bind; outbound dialled directly | Accepted on the clearnet bind / sees our IP | Tagged on the onion bind / through the onion proxy set after Tor login |
"""

import glob
import os
import socket

from test_framework.mininode import P2PInterface
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.torcontrol import TorControlServer
from test_framework.util import assert_equal, assert_raises, p2p_port, tor_port


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

        self.forward_warning = ("Warning: No onion bind is given, so the Tor onion service forwards to the clearnet listener 0.0.0.0:{} "
                                "and its peers are not tagged as Tor. Add -bind=<addr>=onion to listen for them separately.").format(p2p_port(0))
        self.run_bind_scenarios(node, onion_port)
        self.run_tor_scenarios(node, onion_port)

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
        self.stop_node(0, expected_stderr=self.forward_warning)
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

    def start_node_read_log(self, node, extra_args):
        """Start the node and return what it logged while starting."""
        [debug_log] = glob.glob(os.path.join(node.datadir, "*", "debug.log"))
        log_start = os.path.getsize(debug_log)
        self.start_node(0, extra_args=extra_args)
        with open(debug_log, encoding="utf8") as log:
            log.seek(log_start)
            return log.read()

    def peer_binds(self, node, ports):
        """Connect to each port and return the sorted addrbind of each peer."""
        for port in ports:
            node.add_p2p_connection(P2PInterface(node.time_to_connect), dstport=port)
        binds = sorted(peer["addrbind"] for peer in node.getpeerinfo())
        node.disconnect_p2ps()
        return binds

    def assert_not_listening(self, ports):
        for port in ports:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client:
                assert_raises(ConnectionRefusedError, client.connect, ("127.0.0.1", port))

    def run_tor_scenarios(self, node, onion_port):
        """Combinations of -proxy, -listen, -bind and -listenonion, against a fake Tor control port."""
        port = p2p_port(0)
        default_onion_port = port + 1
        clearnet_bind = "-bind=0.0.0.0"
        onion_bind = "-bind=127.0.0.1:{}=onion".format(onion_port)
        tor = TorControlServer(("127.0.0.1", tor_port(1)))
        tor.start()
        tor_args = ["-listenonion=1", "-torcontrol=127.0.0.1:{}".format(tor_port(1))]
        local = "127.0.0.1:{}"

        self.log.info("Tor forwards to the P2P port + 1 when no -bind is given")
        tor.clear()
        self.start_node(0, extra_args=tor_args)
        assert_equal(tor.wait_for_onion_target(), local.format(default_onion_port))
        self.stop_node(0)

        self.log.info("-proxy with a clearnet and an onion bind: Tor forwards to the onion bind")
        tor.clear()
        self.start_node(0, extra_args=tor_args + ["-proxy=127.0.0.1:1", clearnet_bind, onion_bind])
        assert_equal(tor.wait_for_onion_target(), local.format(onion_port))
        assert_equal(self.peer_binds(node, [port, onion_port]), [local.format(port), local.format(onion_port)])
        self.stop_node(0)

        self.log.info("No onion bind: Tor forwards to the clearnet bind, where its peers are not tagged")
        tor.clear()
        self.start_node(0, extra_args=tor_args + [clearnet_bind])
        assert_equal(tor.wait_for_onion_target(), "0.0.0.0:{}".format(port))
        # Tor connects from loopback, so its peers arrive on the clearnet listener like local clearnet ones.
        assert_equal(self.peer_binds(node, [port]), [local.format(port)])
        self.stop_node(0, expected_stderr=self.forward_warning)
        # A -whitelist range covering Tor's address would whitelist them, so it is refused.
        node.assert_start_raises_init_error(
            tor_args + [clearnet_bind, "-whitelist=127.0.0.1"],
            r"Error: -whitelist=127\.0\.0\.1/32 covers 127\.0\.0\.1,",
            match=ErrorMatch.PARTIAL_REGEX)

        self.log.info("-proxy with -listen=1: a clearnet and an onion listener, Tor forwards to the onion one")
        tor.clear()
        self.start_node(0, extra_args=tor_args + ["-proxy=127.0.0.1:1", "-listen=1"])
        assert_equal(tor.wait_for_onion_target(), local.format(default_onion_port))
        assert_equal(self.peer_binds(node, [port, default_onion_port]), [local.format(port), local.format(default_onion_port)])
        self.stop_node(0)

        self.log.info("Tor client: -proxy alone or with -listen=0 opens no listener and no Tor control connection")
        for client_args in (["-proxy=127.0.0.1:1"], ["-proxy=127.0.0.1:1", "-listen=0"]):
            start_up = self.start_node_read_log(node, client_args + tor_args[1:])
            assert "-listen=0 -> setting -listenonion=0" in start_up
            assert "torcontrol thread start" not in start_up
            self.assert_not_listening([port, default_onion_port])
            self.stop_node(0)
        node.assert_start_raises_init_error(
            ["-proxy=127.0.0.1:1", "-listen=0", clearnet_bind, onion_bind],
            "Error: Cannot set -bind or -whitebind together with -listen=0")

        self.log.info("Onion service set up by hand (-listenonion=0): no Tor control connection, onion bind peers tagged")
        start_up = self.start_node_read_log(node, tor_args[1:] + ["-listenonion=0", "-whitelist=127.0.0.1", clearnet_bind, onion_bind])
        assert "torcontrol thread start" not in start_up
        assert_equal(self.peers_whitelisted(node, [port, onion_port]), {
            local.format(port): True,
            local.format(onion_port): False,
        })
        self.stop_node(0)

        self.log.info("No proxy with -listenonion: Tor forwards to the onion bind, outbound peers are dialled directly")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as outbound_peer:
            outbound_peer.bind(("127.0.0.1", tor_port(2)))
            outbound_peer.listen()
            outbound_peer.settimeout(60)
            tor.clear()
            self.start_node(0, extra_args=tor_args + [clearnet_bind, onion_bind, "-connect=127.0.0.1:{}".format(tor_port(2))])
            assert_equal(tor.wait_for_onion_target(), local.format(onion_port))
            outbound_peer.accept()[0].close()
            self.stop_node(0)

        tor.stop()


if __name__ == '__main__':
    OnionWhitelistTest().main()
