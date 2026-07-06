#!/usr/bin/env python3
# Copyright (c) 2025 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that getnetworkinfo's subversion/version fields use the new 3-part
MAJOR.MINOR.BUILD version encoding.

Regression guard for the daemon's advertised subver diverging from the
binary's version label (see FormatSubVersion() in clientversion.cpp and
test_version_config_consistency in src/test/util_tests.cpp for the
in-process equivalent of this check).
"""

import re

from test_framework.test_framework import BitcoinTestFramework


class GetNetworkInfoVersionTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        info = self.nodes[0].getnetworkinfo()

        # Enforce the new 3-part form - catches any regression to the old
        # 4-part "0.7.0.2" form. FormatSubVersion() never inserts a space
        # before the optional parenthesized comment list (see
        # clientversion.cpp), and this codebase has no "rc" suffix concept.
        assert re.match(r'^/Tapyrus Core:\d+\.\d+\.\d+(\([^)]+\))?/$', info["subversion"]), \
            "unexpected subversion format: {}".format(info["subversion"])

        # Cross-check: the integer `version` field must decompose into the
        # same MAJOR.MINOR.BUILD triple (the 1 000 000 / 10 000 / 100 slot
        # encoding from FormatVersion()).
        v = info["version"]
        expected_prefix = "/Tapyrus Core:{}.{}.{}".format(v // 1000000, (v // 10000) % 100, (v // 100) % 100)
        assert info["subversion"].startswith(expected_prefix), \
            "subversion {!r} does not match version {}".format(info["subversion"], v)


if __name__ == '__main__':
    GetNetworkInfoVersionTest().main()
