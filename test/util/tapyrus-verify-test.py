#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test framework for tapyrus-verify.

Reuses tapyrus-util-test.py's bctester()/bctest() harness (same JSON
test-case format, same directory) against a separate JSON file so this
suite can be gated on BUILD_SCRIPT_VERIFY independently of BUILD_UTILS.

Runs automatically during `make check` when BUILD_SCRIPT_VERIFY is on.

Can also be run manually."""
import argparse
try:
    import configparser
except ImportError:
    import ConfigParser as configparser
import importlib.util
import logging
import os

_here = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("tapyrus_util_test", os.path.join(_here, "tapyrus-util-test.py"))
_tapyrus_util_test = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_tapyrus_util_test)
bctester = _tapyrus_util_test.bctester

def main():
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read_file(open(os.path.join(os.path.dirname(__file__), "../config.ini"), encoding="utf8"))
    env_conf = dict(config.items('environment'))

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-v', '--verbose', action='store_true')
    args = parser.parse_args()

    level = logging.DEBUG if args.verbose else logging.ERROR
    logging.basicConfig(format='%(asctime)s - %(levelname)s - %(message)s', level=level)

    bctester(os.path.join(env_conf["SRCDIR"], "test", "util", "data"), "tapyrus-verify-test.json", env_conf)

if __name__ == '__main__':
    main()
