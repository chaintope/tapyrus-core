#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2021 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
TapyrusTxGenerator is An independent python process that can
    - generate transactions on a tapyrus node in real time.
    - or generate a blockchain and wallet for performance test

Transactions are created from the templates in data/templates.json. One template is picked randomly and filled with utxos from the blockchain. Depending on whether we try to make the wallet bigger or smaller during a test we "split" and "join" utxos of randomized size.

In daemon mode this process can be used to fill the blocks in the testnet:
    Transactions may be valid or invalid. Frequency of transaction is configurable.

In benchmark mode it is used to create a wallet with maxUtxoCount(20000) utxos.
* This means that maxUtxoCount(20,000) UTXOs are returned in listunspent.
* How long does it take to execute this command?
* If maxUtxoCount(10,000) of these are used and UTXO is reduced to maxUtxoCount(10,000), what will happen to the execution time of listunspent?
* What happens to the listunspent execution time if all UTXOs are used and set to 0? What happens if I make a new remittance?

"""
from ast import Pass
import threading
import asyncio
import json
import random, time
import os, sys
import decimal
import shutil
import zipfile
import logging, re

#import test framework from parent directory using sys.path
root_folder = os.path.abspath(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(root_folder)

from test_framework.test_node import TestNode, FailedToStartError
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import wait_until, get_datadir_path, initialize_datadir, wait_until, get_rpc_proxy, rpc_url
from test_framework.authproxy import JSONRPCException
from test_framework.blocktools import create_raw_transaction

decimal.getcontext().prec = 4

DAEMON_MAX_BLOCK_COUNT = 9999999999

TESTNET_GENESIS = "01000000000000000000000000000000000000000000000000000000000000000000000044cc181bd0e95c5b999a13d1fc0d193fa8223af97511ad2098217555a841b3518f18ec2536f0bb9d6d4834fcc712e9563840fe9f089db9e8fe890bffb82165849f52ba5e01210366262690cbdf648132ce0c088962c6361112582364ede120f3780ab73438fc4b402b1ed9996920f57a425f6f9797557c0e73d0c9fbafdebcaa796b136e0946ffa98d928f8130b6a572f83da39530b13784eeb7007465b673aa95091619e7ee208501010000000100000000000000000000000000000000000000000000000000000000000000000000000000ffffffff0100f2052a010000002776a92231415132437447336a686f37385372457a4b6533766636647863456b4a74356e7a4188ac00000000"
TESTNET_DATADIR = "~/.tapyrus/tapyrus-testnet"
TESTNET_RPCUSER = "rpcuser"
TESTNET_RPCPASS = "rpcpassword"
TESTNET_IPV4 = "127.0.0.1"
TESTNET_NETWORKID = 1939510133
TESTNET_RPCPORT = 2377
TESTNET_PRIVKEY =  "KxMxt3zYKAjRKKAaDfK8jgH9XQbHnd9HFimmoVQCLdLkDakGMsxu"

class RPCContextFilter(logging.Filter):
    '''class defines a filter to extract RPC name and elapsed time from the test framework's "BitcoinRPC" logger'''
    rpc_name_finder = re.compile("^-(\d+)-> (\w+) ")
    time_finder = re.compile("^<-(\d+)- \[(.*)\] ")
    id = 0

    def filter(self, record):
        self.rpc_name_match = self.rpc_name_finder.match(record.getMessage())
        self.time_match = self.time_finder.match(record.getMessage())

        if self.rpc_name_match is not None:
            self.id =  self.rpc_name_match.group(1)
            self.rpc_name =  self.rpc_name_match.group(2)
            return False

        if self.time_match is not None and self.id == self.time_match.group(1):
            self.elapsed =  self.time_match.group(2)
            
            record.id = self.id
            record.rpc_name = self.rpc_name
            record.elapsed = self.elapsed

            self.id = 0
            self.rpc_name = None
            self.elapsed = None
            return True

class BlockGenertorThread(threading.Thread):
    '''thread to generate blocks. uses asyncio event loop to allow transactions and blocks to be generated in tandem without blocking'''
    block_event_loop = None

    def __init__(self, maxBlockCount, node, signblockprivkey_wif):
        super().__init__(name="BlockGenertorThread")
        # There is only one event loop and no more than one thread must be created
        assert not self.block_event_loop

        BlockGenertorThread.block_event_loop = asyncio.get_event_loop()
        self.maxBlockCount = int(maxBlockCount)
        self.node = node
        self.signblockprivkey_wif = signblockprivkey_wif
        self.log = logging.getLogger('TestFramework')

    def run(self):
        """Schedule block generation."""
        BlockGenertorThread.block_event_loop.create_task(self.generate())
        BlockGenertorThread.block_event_loop.run_forever()

    def close(self, timeout=10):
        """Close the block generation event loop."""
        self.block_event_loop.call_soon_threadsafe(self.block_event_loop.stop)
        wait_until(lambda: not self.block_event_loop.is_running(), timeout=timeout)
        self.block_event_loop.close()
        self.join(timeout)

    async def generate(self):
        while True:
            cnt = self.node.getblockcount()
            self.log.debug("Block %s - %s" %(cnt, self.node.getbestblockhash()))
            BlockGenertorThread.block_event_loop.call_soon(self.node.generate, 1, self.signblockprivkey_wif)
            if self.maxBlockCount > 0 and cnt >= self.maxBlockCount:
                break
            await asyncio.sleep(2)
        loop = asyncio.get_event_loop()
        loop.stop()


class TapyruxTxGenerator():
    '''class used to read a template json file to generate transactions according to the template
    TODO:
    add options to mention signature type(ecdsa/schnorr), multi sig, colored coin etc
    add invalid transctions'''
    templates = []

    def __init__(self, maxUtxoCount, node, shrink=False):
        # initialize from params
        self.maxUtxoCount = int(maxUtxoCount)
        self.node = node
        self.shrink = shrink
        self.log = logging.getLogger('TestFramework')

        #load the transaction templates
        with open(os.path.join(os.path.dirname(os.path.realpath(__file__)), 'data/templates.json'), encoding='utf-8') as f:
            jsonstr = json.load(f)
            self.templates = jsonstr['txs']

    def templateToTx(self, num_inputs, num_outputs, unspent):
        # fill the input and output lists with utxos
        if len(unspent) < num_inputs:
            return (None, None)

        inputs = []
        outputs = []
        amt = {}
        amt['TPC'] = 0

        #fill input list with utxos randomly
        for x in range(0, num_inputs):
            r = random.randint(0, len(unspent)-1)
            utxo = unspent[r]
            # ignore dust
            if( decimal.Decimal(utxo['amount']) - decimal.Decimal(0.05) < 0):
                del(unspent[r])
                continue

            # initialize amt dictionary for colored coins
            if(utxo['token'] not in amt.keys()):
                amt[ utxo['token'] ] = 0

            #add utxo to input list
            amt[ utxo['token'] ] =  amt[ utxo['token'] ] + utxo['amount']
            inputs.append({'txid' : utxo['txid'], 'vout' : utxo['vout']})
            del(unspent[r])
            #if we do not have enough utxos do not continue
            if len(unspent) == 0:
                return (None, None)

        # ignore dust and negative outputs
        outamt = (decimal.Decimal(amt[ utxo['token'] ]) - decimal.Decimal(0.05))/num_outputs
        if outamt < 0:
            return (None, None)

        # fill the output list
        for x in range(0, num_outputs):
            outputs.append({self.node.getnewaddress() : float(outamt) })

        return (inputs, outputs)

    def getInputAndOutputCounts(self, num):
        #choose the transaction templates
        txTemplate = self.templates[num]

        # when the template is a simple join or split we choose a random num of inputs and outputs respectively
        if txTemplate['name'] in ["join", "joinwithchange"]:
            if self.shrink:
                num_inputs = random.randint(1, 20)
            else:
                num_inputs = txTemplate['in']
            num_outputs = txTemplate['out']
        elif txTemplate['name'] in ["split"]:
            num_inputs = txTemplate['in']
            if self.shrink:
                num_outputs = txTemplate['out']
            else:
                num_outputs = random.randint(1, 20)
        else: #"send", "sendwithchange"
            num_inputs = txTemplate['in']
            num_outputs = txTemplate['out']
        return (num_inputs, num_outputs)
    
    async def generatetx(self, expectedUtxoCount):
        while True:
            unspent = self.node.listunspent()
            if len(unspent) == 0:
                await asyncio.sleep(0)
                continue

            if self.shrink and len(unspent) <= expectedUtxoCount:
                break
            elif not self.shrink and self.maxUtxoCount > 0 and len(unspent) > expectedUtxoCount:
                break

            num = random.randint(0, len(self.templates)-1)
            (num_inputs, num_outputs) = self.getInputAndOutputCounts(num)
            (inputs, outputs) = self.templateToTx(num_inputs, num_outputs, unspent)

            if inputs is None:
                continue
            try:
                raw_tx =  self.node.createrawtransaction(inputs, outputs)
                signed_tx =  self.node.signrawtransactionwithwallet(raw_tx)
                self.node.sendrawtransaction(signed_tx['hex'], True)
                await asyncio.sleep(0)
            except Exception as e:
                self.log.debug("[%s] exception ignored" % e.error['message'])
        self.log.debug("Final wallet size : %d" % len(unspent))
        loop = asyncio.get_event_loop()
        loop.stop()

class TestnetDaemonNode():

    def __init__(self, i, datadir, rpcuser, rpcpassword, rpcipv4, rpcport, timewait, use_cli, networkid, log):

        self.process = None
        self.datadir = datadir

        self.cli = None# TestNodeCLI(bitcoin_cli, self.datadir)
        self.use_cli = use_cli

        self.rpc_connected = False
        self.rpc = None
        self.url = "http://%s:%s@%s:%d" % (rpcuser, rpcpassword, rpcipv4, rpcport)
        self.networkid = networkid
        self.rpc_timeout = timewait
        self.coverage_dir = os.getcwd()

        self.p2ps = []
        self.log = log


    def wait_for_rpc_connection(self):
        """Sets up an RPC connection to the testnet tapyrusd process. Returns False if unable to connect."""
        # Poll at a rate of four times per second
        poll_per_s = 4
        for _ in range(poll_per_s * self.rpc_timeout):
            try:
                self.rpc = get_rpc_proxy(self.url, 0, 120, os.getcwd())
                self.rpc.getblockcount()
                # If the call to getblockcount() succeeds then the RPC connection is up
                self.rpc_connected = True
                self.log.debug("RPC connected to %s", self.url)
                return
            except IOError as e:
                #if e.errno != errno.ECONNREFUSED:  # Port not yet open?
                    raise  # unknown IO error
            except JSONRPCException as e:  # Initialization phase
                if e.error['code'] != -28:  # RPC in warmup?
                    raise  # unknown JSON RPC exception
            except ValueError as e:  # cookie file not found and no rpcuser or rpcassword. tapyrusd still starting
                if "No RPC credentials" not in str(e):
                    raise
            time.sleep(1.0 / poll_per_s)
        self._raise_assertion_error("Unable to connect to tapyrusd")

    def __getattr__(self, name):
        """Dispatches any unrecognised messages to the RPC connection or a CLI instance."""
        assert self.rpc_connected and self.rpc is not None, self._node_msg("Error: no RPC connection")
        return getattr(self.rpc, name)


class TapyrusWalletPerformanceTest(BitcoinTestFramework):
    ''' this is the test class. it has three modes, daemon, expand wallet or shrink wallet. these are chosen based on parameters given on cmd line'''

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_chain(self):
        #redirect RPC logging to file
        #logging format is also changed to get only elapsed time for each RPC.
        #RPC output data is filtered out
        rpc_log = logging.getLogger("BitcoinRPC")
        rpc_handler = rpc_log.handlers[0]
        rpc_log.removeHandler(rpc_handler)
        filehandler = logging.FileHandler(self.options.tmpdir + '/rpc_trace.log', encoding='utf-8')
        formatter = logging.Formatter('%(id)-10s %(rpc_name)30s %(elapsed)-15s')
        filter = RPCContextFilter()
        filehandler.setFormatter(formatter)
        rpc_log.addHandler(filehandler)
        rpc_log.addFilter(filter)

        #when we are generating tx in daemon mode or trying to expand the wallet follow normal chain setup
        if self.options.daemon or self.options.maxBlockCount > 0 or self.options.maxUtxoCount > 0:
            return super().setup_chain()

        # in shrink mode since we are using the data directory that is generated already we skip chain setup
        datadir = get_datadir_path(self.options.tmpdir, 0)
        with zipfile.ZipFile(os.path.join(os.path.dirname(os.path.realpath(__file__)), "data/node0.zip"), 'r') as walletarchive:
            walletarchive.extractall(datadir)
        initialize_datadir(self.options.tmpdir, 0)
        self.log.debug("Data dir: %s" % datadir)

    def __del__(self):
        Pass

    async def logsize(self):
        while True:
            unspent = self.nodes[0].listunspent()
            cnt = len(unspent)
            self.log.debug("Wallet size : %d" % cnt)
            await asyncio.sleep(1)

    def setup_nodes(self):
        if not self.options.daemon:
            return super().setup_nodes()

        datadir = initialize_datadir(self.options.daemon_datadir, 0)
        with open(os.path.join(datadir, "genesis.1939510133"), 'w', encoding='utf8') as f:
            f.write(TESTNET_GENESIS)

        node = TestnetDaemonNode(0, datadir, self.options.daemon_rpcuser, self.options.daemon_rpcpassword, self.options.daemon_ipv4, self.options.daemon_rpcport, 90, False, self.options.daemon_networkid, self.log)
        self.nodes.append(node)
        node.wait_for_rpc_connection()

    def daemon_mode(self):
        '''in daemon mode we start a new node in the network using the TestNode class with the config file and genesis block. when the node starts we wait for the blockchain to sync and then generate transactions'''
        self.nodes[0].getblockchaininfo()
        self.log.info("Importing wallet transactions")
        self.nodes[0].importprivkey(TESTNET_PRIVKEY, "testnet", True)
        balance = self.nodes[0].getbalance()

        self.log.info("Starting transaction generator with balance : %d", balance)
        self.tx_generator = TapyruxTxGenerator(DAEMON_MAX_BLOCK_COUNT, self.nodes[0])
        loop = asyncio.get_event_loop()
        loop.create_task(self.tx_generator.generatetx(DAEMON_MAX_BLOCK_COUNT))
        loop.create_task(self.tx_generator.generatetx(DAEMON_MAX_BLOCK_COUNT))
        loop.create_task(self.tx_generator.generatetx(DAEMON_MAX_BLOCK_COUNT))
        loop.create_task(self.logsize())
        loop.run_forever()


    def expand_wallet(self):
        self.log.info("Starting block generator")
        self.nodes[0].importprivkey(self.signblockprivkey_wif)
        self.nodes[0].generate(100, self.signblockprivkey_wif)

        self.block_generator = BlockGenertorThread(self.options.maxBlockCount, self.nodes[0], self.signblockprivkey_wif)
        self.block_generator.start()

        self.log.info("Starting transaction generator")
        self.tx_generator = TapyruxTxGenerator(self.options.maxUtxoCount, self.nodes[0])
        loop = asyncio.get_event_loop()
        loop.create_task(self.tx_generator.generatetx(self.options.maxUtxoCount))
        loop.create_task(self.tx_generator.generatetx(self.options.maxUtxoCount))
        loop.create_task(self.tx_generator.generatetx(self.options.maxUtxoCount))
        loop.create_task(self.logsize())
        self.block_generator.join()

        #loop ends when the max tx count or max block count is met
        self.nodes[0].dumpwallet(str(os.path.join(os.path.dirname(os.path.realpath(__file__)), str.format("wallet_%s" % os.path.basename(self.options.tmpdir)))))
        shutil.make_archive(os.path.join(os.path.dirname(os.path.realpath(__file__)), str.format("node0_%s" % os.path.basename(self.options.tmpdir))), 'zip', os.path.join(self.options.tmpdir, "node0"))

    def shrink_wallet(self):
        '''in performance test we load the 20000 utxo wallet and try to get the elapsed time for each RPC call
        i. first until we spend half of the wallet utxos
        ii. until all utxos are spent
        '''
        self.log.info("Import wallet")
        self.nodes[0].importprivkey(self.signblockprivkey_wif)
        self.nodes[0].importwallet(os.path.join(os.path.dirname(os.path.realpath(__file__)), "data/wallet20000utxo"))
        asyncio.sleep(2)

        self.log.info("Rescan blockchain")
        self.nodes[0].rescanblockchain()
        self.options.maxUtxoCount =len(self.nodes[0].listunspent())
        self.log.info("Wallet size :%d utxos" %self.options.maxUtxoCount)

        self.log.info("Starting block generator")
        self.block_generator = BlockGenertorThread(self.options.maxBlockCount, self.nodes[0], self.signblockprivkey_wif)
        self.block_generator.start()

        self.log.info("Starting transaction generator - shrink to half")
        self.tx_generator = TapyruxTxGenerator(self.options.maxUtxoCount , self.nodes[0], True)
        loop = asyncio.get_event_loop()
        loop.create_task(self.logsize())
        loop.create_task(self.tx_generator.generatetx(self.options.maxUtxoCount/2))
        loop.create_task(self.tx_generator.generatetx(self.options.maxUtxoCount/2))
        self.block_generator.join()
        loop.run_forever()

        midUtxoCount =len(self.nodes[0].listunspent())
        self.log.info("Wallet size :%d utxos" % midUtxoCount)

        self.log.info("Starting transaction generator - shrink to 0")
        self.tx_generator = TapyruxTxGenerator(self.options.maxUtxoCount, self.nodes[0], True)
        loop = asyncio.get_event_loop()
        loop.create_task(self.logsize())
        loop.create_task(self.tx_generator.generatetx(0))
        loop.create_task(self.tx_generator.generatetx(0))
        self.block_generator.join()
        loop.run_forever()

        endUtxoCount =len(self.nodes[0].listunspent())
        self.log.info("Wallet size :%d utxos" % endUtxoCount)

        self.log.info("Starting transaction generator")
        self.tx_generator = TapyruxTxGenerator(10, self.nodes[0])
        loop = asyncio.get_event_loop()
        loop.create_task(self.logsize())
        loop.create_task(self.tx_generator.generatetx(0))
        self.block_generator.join()
        loop.run_forever()

    def run_test(self):
        if self.options.daemon:
            return self.daemon_mode()
        elif self.options.maxBlockCount > 0 or self.options.maxUtxoCount > 0:
            return self.expand_wallet()
        else:
            self.options.shrink = True
            return self.shrink_wallet()

    def add_options(self, parser):
        ''' options:
        1. daemon - mode to run forever(point to a testnet node or production node)
                    or stopatcount i.e stop when maxBlockCount=x or maxUtxoCount=x
        2. maxBlockCount=x
        3. maxUtxoCount=x
        '''
        parser.add_argument("--daemon", dest="daemon", default=False, action="store_true",
                            help="Start in daemon mode")
        parser.add_argument("--daemon_datadir", dest="daemon_datadir",
                            default=TESTNET_DATADIR, help="data directory for daemon mode. default is testnet")
        parser.add_argument("--daemon_genesis", dest="daemon_genesis",
                            default=TESTNET_GENESIS, help="genesis block hex or daemon mode. default is testnet")
        parser.add_argument("--daemon_networkid", dest="daemon_networkid",
                            default=TESTNET_NETWORKID, help="Networkid of testnet")
        parser.add_argument("--daemon_rpcport", dest="daemon_rpcport",
                            default=TESTNET_RPCPORT, help="RPC port number of testnet node")
        parser.add_argument("--daemon_rpcuser", dest="daemon_rpcuser",
                            default=TESTNET_RPCUSER, help="RPC username of testnet node")
        parser.add_argument("--daemon_rpcpassword", dest="daemon_rpcpassword",
                            default=TESTNET_RPCPASS, help="RPC password of testnet node")
        parser.add_argument("--daemon_ipv4", dest="daemon_ipv4",
                            default=TESTNET_IPV4, help="IPv4 address of testnet node. default is localhost")
        parser.add_argument("--daemon_privkey", dest="daemon_privkey",
                            default=TESTNET_PRIVKEY, help="Sign Block Private key of testnet. default is testnet key")
        parser.add_argument("--maxBlockCount", dest="maxBlockCount", default=0,
                            help="Stop the node when maxBlockCount blocks are reached")
        parser.add_argument("--maxUtxoCount", dest="maxUtxoCount", default=0,
                            help="Stop the node when maxUtxoCount utxos are reached in the wallet")

if __name__ == '__main__':
    TapyrusWalletPerformanceTest().main()

