#!/usr/bin/env python3
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Tests for IPv6 support: P2P, -addnode, -connect, -externalip, -whitelist, -rpcallowip, ban persistence."""

import os

from test_framework.netutil import test_ipv6_local
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    NetworkDirName,
    assert_equal,
    assert_raises_rpc_error,
    p2p_port,
    wait_until,
)

# A globally routable IPv6 address for -externalip testing.  Avoid RFC 3849
# (2001:db8::/32) so this doesn't look like a copy-pasted example — that block
# is topologically routable (IsValid()/IsRoutable() don't reject it, it's just
# reserved for documentation). Also avoid RFC 4193 (fc00::/7), link-local
# (fe80::/10), loopback (::1), or any other non-routable range.
_ROUTABLE_IPV6 = "2603:3005::1"


class IPv6Test(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # Disable automatic bind=127.0.0.1 so nodes default-bind to :: and
        # 0.0.0.0, making them reachable on both [::1] and 127.0.0.1.
        self.bind_to_localhost_only = False

    def setup_network(self):
        # Do NOT auto-connect via 127.0.0.1 — each sub-test sets up its own
        # IPv4/IPv6 topology, and _test_p2p_via_ipv6 in particular needs the
        # only peer on node0 to be the one it establishes over [::1].
        self.setup_nodes()

    def run_test(self):
        if not test_ipv6_local():
            self.log.warning("No IPv6 loopback support — skipping all IPv6 sub-tests")
            return

        self._test_p2p_via_ipv6()
        self._test_addnode_ipv6()
        self._test_connect_flag_ipv6()
        self._test_externalip_valid_ipv6()
        self._test_externalip_linklocal_rejected()
        self._test_whitelist_ipv6()
        self._test_rpcallowip_ipv6()
        self._test_ban_ipv6_persistence()

    # ──────────────────────────────────────────── helpers

    def _debug_log(self, node):
        path = os.path.join(node.datadir, NetworkDirName(), "debug.log")
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()

    # ──────────────────────────────────────────── sub-tests

    def _test_p2p_via_ipv6(self):
        self.log.info("P2P: connect two nodes via [::1]")
        assert_equal(self.nodes[0].getpeerinfo(), [])
        assert_equal(self.nodes[1].getpeerinfo(), [])

        peer_addr = "[::1]:{}".format(p2p_port(1))
        self.nodes[0].addnode(peer_addr, "onetry")
        wait_until(lambda: len(self.nodes[0].getpeerinfo()) > 0, timeout=15)
        wait_until(lambda: len(self.nodes[1].getpeerinfo()) > 0, timeout=15)

        # node0 must see exactly one outbound, manually-added peer at the
        # [::1] address it was told to connect to.
        peers0 = self.nodes[0].getpeerinfo()
        assert_equal(len(peers0), 1)
        assert_equal(peers0[0]["addr"], peer_addr)
        assert_equal(peers0[0]["inbound"], False)
        assert_equal(peers0[0]["addnode"], True)

        # node1 must see exactly one inbound peer, arriving over IPv6.
        peers1 = self.nodes[1].getpeerinfo()
        assert_equal(len(peers1), 1)
        assert_equal(peers1[0]["inbound"], True)
        assert "::" in peers1[0]["addr"], \
            "Expected IPv6 peer addr on node1, got: {}".format(peers1[0]["addr"])

        self.nodes[0].generate(1, self.signblockprivkey_wif)
        wait_until(lambda: self.nodes[1].getblockcount() == 1, timeout=15)
        # Confirm the block reached node1 via this specific IPv6 connection,
        # not merely that node1's tip advanced by some other means.
        wait_until(lambda: self.nodes[1].getpeerinfo()[0]["synced_blocks"] == 1, timeout=15)
        self.log.info("  block propagated over IPv6 P2P connection")

    def _test_addnode_ipv6(self):
        self.log.info("addnode: IPv4 and IPv6 addresses accepted without error")
        node = self.nodes[0]
        node.addnode("127.0.0.1:{}".format(p2p_port(1)), "onetry")
        self.log.info("  addnode 127.0.0.1:port accepted")
        node.addnode("[::1]:{}".format(p2p_port(1)), "onetry")
        self.log.info("  addnode [::1]:port accepted")

    def _test_connect_flag_ipv6(self):
        self.log.info("-connect: node 1 connects out to node 0 via [::1]")
        self.restart_node(1, extra_args=[
            "-connect=[::1]:{}".format(p2p_port(0)),
        ])
        wait_until(lambda: len(self.nodes[1].getpeerinfo()) > 0, timeout=15)
        peer_addr = self.nodes[1].getpeerinfo()[0]["addr"]
        assert "::" in peer_addr, \
            "Expected IPv6 peer addr after -connect=[::1]:port, got: {}".format(peer_addr)
        self.log.info("  -connect=[::1]:port established IPv6 outbound connection")

    def _test_externalip_valid_ipv6(self):
        self.log.info("-externalip: globally routable IPv6 address added to localaddresses")
        self.restart_node(1, extra_args=[
            "-externalip=[{}]".format(_ROUTABLE_IPV6),
        ])
        local_addrs = [
            a["address"]
            for a in self.nodes[1].getnetworkinfo()["localaddresses"]
        ]
        assert _ROUTABLE_IPV6 in local_addrs, \
            "Expected {} in localaddresses: {}".format(_ROUTABLE_IPV6, local_addrs)
        self.log.info("  {} present in getnetworkinfo.localaddresses".format(_ROUTABLE_IPV6))

    def _test_externalip_linklocal_rejected(self):
        self.log.info("-externalip: link-local address rejected with log warning")
        log_before = len(self._debug_log(self.nodes[1]))
        self.restart_node(1, extra_args=["-externalip=[fe80::1]"])
        new_log = self._debug_log(self.nodes[1])[log_before:]
        assert "ignoring non-routable address" in new_log, \
            "Expected link-local rejection warning in debug.log after restart"
        local_addrs = [
            a["address"]
            for a in self.nodes[1].getnetworkinfo()["localaddresses"]
        ]
        assert "fe80::1" not in local_addrs, \
            "fe80::1 must not appear in localaddresses"
        self.log.info("  fe80::1 rejected with warning, absent from localaddresses")

    def _test_whitelist_ipv6(self):
        self.log.info("-whitelist: IPv4 CIDR and IPv6 CIDR both accepted at startup")
        self.restart_node(1, extra_args=["-whitelist=127.0.0.0/8"])
        assert self.nodes[1].getblockcount() >= 0
        self.log.info("  -whitelist=127.0.0.0/8 accepted")
        self.restart_node(1, extra_args=["-whitelist=2001:db8::/32"])
        assert self.nodes[1].getblockcount() >= 0
        self.log.info("  -whitelist=2001:db8::/32 accepted")

    def _test_rpcallowip_ipv6(self):
        self.log.info("-rpcallowip: IPv4 subnet and IPv6/128 subnet both accepted")
        # 127.0.0.0/8 is always added as a default; adding ::1/128 explicitly
        # exercises the IPv6 CSubNet parsing path in InitHTTPAllowList.
        self.restart_node(1, extra_args=["-rpcallowip=::1/128"])
        assert self.nodes[1].getblockcount() >= 0
        self.log.info("  -rpcallowip=::1/128 accepted (127.0.0.0/8 added by default)")

    def _test_ban_ipv6_persistence(self):
        self.log.info("Ban: IPv6 /32 subnet ban survives node restart; invalid address rejected")
        self.restart_node(1, extra_args=[])
        node = self.nodes[1]

        # Invalid address must produce an RPC error.
        assert_raises_rpc_error(-30, "Error: Invalid IP/Subnet",
                                node.setban, "not_an_ip", "add")
        self.log.info("  setban with invalid address raised RPC error")

        # Valid IPv6 CIDR ban.
        node.setban("2001:db8::/32", "add")
        banned = {b["address"] for b in node.listbanned()}
        assert "2001:db8::/32" in banned, \
            "Expected 2001:db8::/32 in ban list: {}".format(banned)
        self.log.info("  2001:db8::/32 added to ban list")

        # Ban must persist across restart.
        self.restart_node(1, extra_args=[])
        banned_after = {b["address"] for b in self.nodes[1].listbanned()}
        assert "2001:db8::/32" in banned_after, \
            "Expected 2001:db8::/32 to persist after restart: {}".format(banned_after)

        self.nodes[1].setban("2001:db8::/32", "remove")
        self.log.info("  2001:db8::/32 ban persisted across restart")


if __name__ == "__main__":
    IPv6Test().main()
