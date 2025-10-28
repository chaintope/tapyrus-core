#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Base class for RPC testing."""

import configparser
from enum import Enum
import logging
import argparse
import os
import pdb
import shutil
import sys
import tempfile
import time
from subprocess import TimeoutExpired

from .authproxy import JSONRPCException
from . import coverage
from .test_node import TestNode
from .mininode import NetworkThread
from .timeout_config import TAPYRUSD_MIN_TIMEOUT, TAPYRUSD_PROC_TIMEOUT, TAPYRUSD_SYNC_TIMEOUT
from .blocktools import createTestGenesisBlock
from .util import (
    MAX_NODES,
    PortSeed,
    assert_equal,
    check_json_precision,
    connect_nodes_bi,
    disconnect_nodes,
    get_datadir_path,
    initialize_datadir,
    p2p_port,
    set_node_times,
    sync_blocks,
    sync_mempools,
    bytes_to_hex_str,
    NetworkDirName,
    TAPYRUS_MODES
)

class TestStatus(Enum):
    PASSED = 1
    FAILED = 2
    SKIPPED = 3
    TIMEOUT = 4

TEST_EXIT_PASSED = 0
TEST_EXIT_FAILED = 1
TEST_EXIT_SKIPPED = 77
TEST_EXIT_TIMEOUT = 124


class BitcoinTestMetaClass(type):
    """Metaclass for BitcoinTestFramework.

    Ensures that any attempt to register a subclass of `BitcoinTestFramework`
    adheres to a standard whereby the subclass overrides `set_test_params` and
    `run_test` but DOES NOT override either `__init__` or `main`. If any of
    those standards are violated, a ``TypeError`` is raised."""

    def __new__(cls, clsname, bases, dct):
        if not clsname == 'BitcoinTestFramework':
            if not ('run_test' in dct and 'set_test_params' in dct):
                raise TypeError("BitcoinTestFramework subclasses must override "
                                "'run_test' and 'set_test_params'")
            if '__init__' in dct or 'main' in dct:
                raise TypeError("BitcoinTestFramework subclasses may not override "
                                "'__init__' or 'main'")

        return super().__new__(cls, clsname, bases, dct)


class BitcoinTestFramework(metaclass=BitcoinTestMetaClass):
    """Base class for a bitcoin test script.

    Individual bitcoin test scripts should subclass this class and override the set_test_params() and run_test() methods.

    Individual tests can also override the following methods to customize the test setup:

    - add_options()
    - setup_chain()
    - setup_network()
    - setup_nodes()

    The __init__() and main() methods should not be overridden.

    This class also contains various public and private helper methods."""

    def __init__(self):
        """Sets test framework defaults. Do not override this method. Instead, override the set_test_params() method"""
        self.setup_clean_chain = False
        self.nodes = []
        self.network_thread = None
        self.mocktime = 0
        self.rpc_timewait = TAPYRUSD_PROC_TIMEOUT  # Wait for up to x min for the RPC server to respond
        self.supports_cli = False
        self.bind_to_localhost_only = True
        self.signblockpubkey = "025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3"
        self.signblockprivkey = "67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37"
        self.signblockprivkey_wif = "cR4F4fGuKjDWxiYDtGtyM77WkrVhTgokVyM2ERxoxp7R4RQP9dgE"
        self.genesisBlock = None
        self.mode = TAPYRUS_MODES.DEV
        self.set_test_params()

        assert hasattr(self, "num_nodes"), "Test must set self.num_nodes in set_test_params()"

    def main(self):
        """Main function. This should not be overridden by the subclass test scripts."""

        parser = argparse.ArgumentParser(usage="%(prog)s [options]")
        parser.add_argument("--nocleanup", dest="nocleanup", default=False, action="store_true",
                            help="Leave tapyrusds and test.* datadir on exit or error")
        parser.add_argument("--noshutdown", dest="noshutdown", default=False, action="store_true",
                            help="Don't stop tapyrusds after the test execution")
        parser.add_argument("--cachedir", dest="cachedir", default=os.path.abspath(os.path.dirname(os.path.realpath(__file__)) + "/../../cache"),
                            help="Directory for caching pregenerated datadirs (default: %(default)s)")
        parser.add_argument("--tmpdir", dest="tmpdir", help="Root directory for datadirs")
        parser.add_argument("-l", "--loglevel", dest="loglevel", default="INFO",
                            help="log events at this level and higher to the console. Can be set to DEBUG, INFO, WARNING, ERROR or CRITICAL. Passing --loglevel DEBUG will output all logs to console. Note that logs at all levels are always written to the test_framework.log file in the temporary test directory.")
        parser.add_argument("--tracerpc", dest="trace_rpc", default=False, action="store_true",
                            help="Print out all RPC calls as they are made")
        parser.add_argument("--portseed", dest="port_seed", default=os.getpid(), type=int,
                            help="The seed to use for assigning port numbers (default: current process id)")
        parser.add_argument("--coveragedir", dest="coveragedir",
                            help="Write tested RPC commands into this directory")
        parser.add_argument("--configfile", dest="configfile",
                            default=os.path.abspath(os.path.dirname(os.path.realpath(__file__)) + "/../../config.ini"),
                            help="Location of the test framework config file (default: %(default)s)")
        parser.add_argument("--pdbonfailure", dest="pdbonfailure", default=False, action="store_true",
                            help="Attach a python debugger if test fails")
        parser.add_argument("--usecli", dest="usecli", default=False, action="store_true",
                            help="use tapyrus-cli instead of RPC for all commands")
        parser.add_argument("--scheme", dest="scheme", default="ECDSA", 
                            help="use ECDSA/SCHNORR signature scheme in sign transaction RPCs")
        self.add_options(parser)
        self.options = parser.parse_args()

        PortSeed.n = self.options.port_seed

        check_json_precision()

        self.options.cachedir = os.path.abspath(self.options.cachedir)

        config = configparser.ConfigParser()
        config.read_file(open(self.options.configfile))
        bin_path_cmake = os.path.join(config["environment"]["BUILDDIR"] + "/bin/")
        bin_path_autotools = os.path.join(config["environment"]["BUILDDIR"] + "/src/")

        if os.path.exists(os.path.join(bin_path_cmake, 'tapyrusd' + config["environment"]["EXEEXT"])):
            default_bin_path = bin_path_cmake
        else:
            default_bin_path = bin_path_autotools
        self.options.bitcoind = os.getenv("TAPYRUSD", default=os.path.join(default_bin_path + 'tapyrusd' + config["environment"]["EXEEXT"]))
        self.options.bitcoincli = os.getenv("TAPYRUSCLI", default=os.path.join(default_bin_path + 'tapyrus-cli' + config["environment"]["EXEEXT"]))

        os.environ['PATH'] = os.pathsep.join([
            default_bin_path,
            os.path.join(default_bin_path, 'qt'),
            os.environ['PATH']
        ])
        # Set up temp directory and start logging
        if self.options.tmpdir:
            self.options.tmpdir = os.path.abspath(self.options.tmpdir)
            os.makedirs(self.options.tmpdir, exist_ok=False)
        else:
            self.options.tmpdir = tempfile.mkdtemp(prefix="test")
        self._start_logging()

        self.log.debug('Setting up network thread')
        self.network_thread = NetworkThread()
        self.network_thread.start()

        success = TestStatus.FAILED

        try:
            if self.options.usecli and not self.supports_cli:
                raise SkipTest("--usecli specified but test does not support using CLI")
            self.setup_chain()
            self.setup_network()
            self.log.info("Test using %s signature scheme" % self.options.scheme)
            self.run_test()
            success = TestStatus.PASSED
        except JSONRPCException as e:
            self.log.exception("JSONRPC error")
        except TimeoutError as e:
            success = TestStatus.TIMEOUT
            self.log.warning("Timeout. %s" % e.strerror)
        except TimeoutExpired as e:
            success = TestStatus.TIMEOUT
            self.log.warning("Timeout. %s" % e.stderr)
        except SkipTest as e:
            self.log.warning("Test Skipped: %s" % e.message)
            success = TestStatus.SKIPPED
        except AssertionError as e:
            self.log.exception("Assertion failed")
        except KeyError as e:
            self.log.exception("Key error")
        except Exception as e:
            self.log.exception("Unexpected exception caught during testing")
        except KeyboardInterrupt as e:
            self.log.warning("Exiting after keyboard interrupt")

        if success == TestStatus.FAILED and self.options.pdbonfailure:
            print("Testcase failed. Attaching python debugger. Enter ? for help")
            pdb.set_trace()

        self.log.debug('Closing down network thread')
        self.network_thread.close()
        if not self.options.noshutdown:
            self.log.info("Stopping nodes")
            if self.nodes:
                self.stop_nodes()
        else:
            for node in self.nodes:
                node.cleanup_on_exit = False
            self.log.info("Note: tapyrusd(s) were not stopped and may still be running")

        if not self.options.nocleanup and not self.options.noshutdown and success != TestStatus.FAILED and success != TestStatus.TIMEOUT:
            self.log.info("Cleaning up {} on exit".format(self.options.tmpdir))
            cleanup_tree_on_exit = True
        else:
            self.log.warning("Not cleaning up dir %s" % self.options.tmpdir)
            cleanup_tree_on_exit = False

        if success == TestStatus.PASSED:
            self.log.info("Tests successful")
            exit_code = TEST_EXIT_PASSED
        elif success == TestStatus.SKIPPED:
            self.log.info("Test skipped")
            exit_code = TEST_EXIT_SKIPPED
        elif success == TestStatus.TIMEOUT:
            self.log.info("Test timeout")
            exit_code = TEST_EXIT_TIMEOUT
        else:
            self.log.error("Test failed")
            exit_code = TEST_EXIT_FAILED
        
        if cleanup_tree_on_exit:
            logging.shutdown()
            shutil.rmtree(self.options.tmpdir)
        else:
            self.log.error("Test logging available at %s/test_framework.log", self.options.tmpdir)
            self.log.error("Hint: Call {} '{}' to consolidate all logs".format(os.path.normpath(os.path.dirname(os.path.realpath(__file__)) + "/../combine_logs.py"), self.options.tmpdir))
            logging.shutdown()

        sys.exit(exit_code)

    # Methods to override in subclass test scripts.
    def set_test_params(self):
        """Tests must this method to change default values for number of nodes, topology, etc"""
        raise NotImplementedError

    def add_options(self, parser):
        """Override this method to add command-line options to the test"""
        pass

    def setup_chain(self):
        """Override this method to customize blockchain setup"""
        self.log.info("Initializing test directory " + self.options.tmpdir)
        if self.setup_clean_chain:
            self._initialize_chain_clean()
        else:
            self._initialize_chain()

    def setup_network(self):
        """Override this method to customize test network topology"""
        self.setup_nodes()

        # Connect the nodes as a "chain".  This allows us
        # to split the network between nodes 1 and 2 to get
        # two halves that can work on competing chains.
        for i in range(self.num_nodes - 1):
            connect_nodes_bi(self.nodes, i, i + 1)
        self.sync_all()

    def setup_nodes(self):
        """Override this method to customize test node setup"""
        extra_args = None
        if hasattr(self, "extra_args"):
            extra_args = self.extra_args
        self.add_nodes(self.num_nodes, extra_args)
        self.start_nodes()

    def run_test(self):
        """Tests must override this method to define test logic"""
        raise NotImplementedError

    # Public helper methods. These can be accessed by the subclass test scripts.

    def add_nodes(self, num_nodes, extra_args=None, *, rpchost=None, binary=None):
        """Instantiate TestNode objects"""
        if self.bind_to_localhost_only:
            extra_confs = [["bind=127.0.0.1"]] * num_nodes
        else:
            extra_confs = [[]] * num_nodes
        if extra_args is None:
            extra_args = [[]] * num_nodes
        if binary is None:
            binary = [self.options.bitcoind] * num_nodes
        assert_equal(len(extra_confs), num_nodes)
        assert_equal(len(extra_args), num_nodes)
        assert_equal(len(binary), num_nodes)
        for i in range(num_nodes):
            self.nodes.append(TestNode(i, get_datadir_path(self.options.tmpdir, i), rpchost=rpchost, timewait=self.rpc_timewait, bitcoind=binary[i], bitcoin_cli=self.options.bitcoincli, mocktime=self.mocktime, coverage_dir=self.options.coveragedir, signblockpubkey=self.signblockpubkey, extra_conf=extra_confs[i], extra_args=extra_args[i], use_cli=self.options.usecli, networkid=self.mode.value))

    def start_node(self, i, *args, **kwargs):
        """Start a bitcoind"""

        node = self.nodes[i]

        # Remove timeout from kwargs if present (it's for RPC wait, not subprocess)
        timeout = kwargs.pop('timeout', None)
        if timeout is not None:
            node.rpc_timeout = timeout

        node.start(*args, **kwargs)
        elapsed = node.wait_for_rpc_connection()
        self.log.debug("Time taken %s" % elapsed)

        if self.options.coveragedir is not None:
            coverage.write_all_rpc_commands(self.options.coveragedir, node.rpc)

    def start_nodes(self, extra_args=None, *args, **kwargs):
        """Start multiple bitcoinds"""

        if extra_args is None:
            extra_args = [None] * self.num_nodes
        assert_equal(len(extra_args), self.num_nodes)
        try:
            for i, node in enumerate(self.nodes):
                node.start(extra_args[i], *args, **kwargs)
            for node in self.nodes:
                node.wait_for_rpc_connection()
        except:
            # If one node failed to start, stop the others
            self.stop_nodes()
            raise

        if self.options.coveragedir is not None:
            for node in self.nodes:
                coverage.write_all_rpc_commands(self.options.coveragedir, node.rpc)

    def stop_node(self, i, expected_stderr=''):
        """Stop a bitcoind test node"""
        self.nodes[i].stop_node(expected_stderr)
        self.nodes[i].wait_until_stopped()

    def stop_nodes(self):
        """Stop multiple bitcoind test nodes"""
        for node in self.nodes:
            # Issue RPC to stop nodes
            node.stop_node()

        for node in self.nodes:
            # Wait for nodes to stop
            node.wait_until_stopped()

    def restart_node(self, i, extra_args=None):
        """Stop and start a test node"""
        self.stop_node(i)
        self.start_node(i, extra_args)

    def wait_for_node_exit(self, i, timeout):
        self.nodes[i].process.wait(timeout)

    def split_network(self):
        """
        Split the network of four nodes into nodes 0/1 and 2/3.
        """
        disconnect_nodes(self.nodes[1], 2)
        disconnect_nodes(self.nodes[2], 1)
        self.sync_all([self.nodes[:2], self.nodes[2:]])

    def join_network(self):
        """
        Join the (previously split) network halves together.
        """
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_all()

    def sync_all(self, node_groups=None):
        if not node_groups:
            node_groups = [self.nodes]

        for group in node_groups:
            sync_blocks(group)
            sync_mempools(group)

    def enable_mocktime(self):
        """Enable mocktime for the script.

        mocktime may be needed for scripts that use the cached version of the
        blockchain.  If the cached version of the blockchain is used without
        mocktime then the mempools will not sync due to IBD.

        For backward compatibility of the python scripts with previous
        versions of the cache, this helper function sets mocktime to Jan 1,
        2014 + (201 * 10 * 60)"""
        self.mocktime = 1546853016 + (201 * 10 * 60)

    def disable_mocktime(self):
        self.mocktime = 0

    # Private helper methods. These should not be accessed by the subclass test scripts.

    def _start_logging(self):
        # Add logger and logging handlers
        self.log = logging.getLogger('TestFramework')
        self.log.setLevel(logging.DEBUG)
        # Create file handler to log all messages
        fh = logging.FileHandler(self.options.tmpdir + '/test_framework.log', encoding='utf-8')
        fh.setLevel(logging.DEBUG)
        # Create console handler to log messages to stderr. By default this logs only error messages, but can be configured with --loglevel.
        ch = logging.StreamHandler(sys.stdout)
        # User can provide log level as a number or string (eg DEBUG). loglevel was caught as a string, so try to convert it to an int
        ll = int(self.options.loglevel) if self.options.loglevel.isdigit() else self.options.loglevel.upper()
        ch.setLevel(ll)
        # Format logs the same as bitcoind's debug.log with microprecision (so log files can be concatenated and sorted)
        formatter = logging.Formatter(fmt='%(asctime)s.%(msecs)03d000Z %(name)s (%(levelname)s): %(message)s', datefmt='%Y-%m-%dT%H:%M:%S')
        formatter.converter = time.gmtime
        fh.setFormatter(formatter)
        ch.setFormatter(formatter)
        # add the handlers to the logger
        self.log.addHandler(fh)
        self.log.addHandler(ch)

        if self.options.trace_rpc:
            rpc_logger = logging.getLogger("BitcoinRPC")
            rpc_logger.setLevel(logging.DEBUG)
            rpc_handler = logging.StreamHandler(sys.stdout)
            rpc_handler.setLevel(logging.DEBUG)
            rpc_logger.addHandler(rpc_handler)

    def _initialize_chain(self):
        """Initialize a pre-mined blockchain for use by the test.

        Create a cache of a 200-block-long chain (with wallet) for MAX_NODES
        Afterward, create num_nodes copies from the cache."""

        assert self.num_nodes <= MAX_NODES
        create_cache = False
        for i in range(MAX_NODES):
            if not os.path.isdir(get_datadir_path(self.options.cachedir, i)):
                create_cache = True
                break

        if create_cache:
            self.log.debug("Creating data directories from cached datadir")

            # find and delete old cache directories if any exist
            for i in range(MAX_NODES):
                if os.path.isdir(get_datadir_path(self.options.cachedir, i)):
                    shutil.rmtree(get_datadir_path(self.options.cachedir, i))

            self.enable_mocktime()

            # Create cache directories, run bitcoinds:
            for i in range(MAX_NODES):
                datadir = initialize_datadir(self.options.cachedir, i)
                self.writeGenesisBlockToFile(datadir, nTime= self.mocktime - (201 * 10 * 60))
                args = [self.options.bitcoind,
                "-datadir=" + datadir]
                if i > 0:
                    args.append("-connect=127.0.0.1:" + str(p2p_port(0)))
                    args.append("-debug=all")
                self.nodes.append(TestNode(i, get_datadir_path(self.options.cachedir, i), signblockpubkey=self.signblockpubkey, extra_conf=["bind=127.0.0.1"], extra_args=[], rpchost=None, timewait=self.rpc_timewait, bitcoind=self.options.bitcoind, bitcoin_cli=self.options.bitcoincli, mocktime=self.mocktime, coverage_dir=None, networkid=self.mode.value))
                self.nodes[i].args = args
                self.start_node(i)

            # Wait for RPC connections to be ready
            for node in self.nodes:
                node.wait_for_rpc_connection()

            # Create a 100 block-block-long chain; each of the 4 first nodes
            # gets 25 mature blocks.
            # Note: To preserve compatibility with older versions of
            # initialize_chain, only 4 nodes will generate coins.
            #
            # blocks are created with timestamps 10 minutes apart
            # starting from 2010 minutes in the past
            block_time = self.mocktime - (201 * 10 * 60)
            for peer in range(4):
                for j in range(25):
                    set_node_times(self.nodes, block_time)
                    self.nodes[peer].generate(1, self.signblockprivkey_wif)
                    block_time += 10 * 60
                # Must sync before next peer starts generating blocks
                sync_blocks(self.nodes)

            # Shut them down, and clean up cache directories:
            self.stop_nodes()
            self.nodes = []
            self.disable_mocktime()

            def cache_path(n, *paths):
                return os.path.join(get_datadir_path(self.options.cachedir, n), NetworkDirName(), *paths)

            for i in range(MAX_NODES):
                for entry in os.listdir(cache_path(i)):
                    if entry not in ['wallets', 'chainstate', 'blocks', 'genesis.dat']:
                        os.remove(cache_path(i, entry))

        for i in range(self.num_nodes):
            from_dir = get_datadir_path(self.options.cachedir, i)
            to_dir = get_datadir_path(self.options.tmpdir, i)
            shutil.copytree(from_dir, to_dir)
            datadir = initialize_datadir(self.options.tmpdir, i)  # Overwrite port/rpcport in tapyrus.conf

    def _initialize_chain_clean(self):
        """Initialize empty blockchain for use by the test.

        Create an empty blockchain and num_nodes wallets.
        Useful if a test case wants complete control over initialization."""
        for i in range(self.num_nodes):
            datadir = initialize_datadir(self.options.tmpdir, i)
            self.writeGenesisBlockToFile(datadir)

    def writeGenesisBlockToFile(self, datadir, networkid = None, nTime=None):
        if(networkid != None):
            filename = "genesis.%d" % networkid
        else:
            filename = "genesis.dat"
        if self.genesisBlock == None:
            self.genesisBlock = createTestGenesisBlock(self.signblockpubkey, self.signblockprivkey, nTime)
        with open(os.path.join(datadir, filename), 'w', encoding='utf8') as f:
            f.write(bytes_to_hex_str(self.genesisBlock.serialize()))

class SkipTest(Exception):
    """This exception is raised to skip a test"""
    def __init__(self, message):
        self.message = message


def skip_if_no_py3_zmq():
    """Attempt to import the zmq package and skip the test if the import fails."""
    try:
        import zmq  # noqa
    except ImportError:
        raise SkipTest("python3-zmq module not available.")


def skip_if_no_bitcoind_zmq(test_instance):
    """Skip the running test if bitcoind has not been compiled with zmq support."""
    if not is_zmq_enabled(test_instance):
        raise SkipTest("bitcoind has not been built with zmq enabled.")


def is_zmq_enabled(test_instance):
    """Checks whether zmq is enabled or not."""
    config = configparser.ConfigParser()
    config.read_file(open(test_instance.options.configfile))

    return config["components"].getboolean("ENABLE_ZMQ")
