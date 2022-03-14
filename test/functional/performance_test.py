#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2021 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
In benchmark mode it is used to create a wallet with 20000 utxos.
* This means that 20,000 UTXOs are returned in listunspent.
* How long does it take to execute this command?
* If 10,000 of these are used and UTXO is reduced to 10,000, what will happen to the execution time of listunspent?
* What happens to the listunspent execution time if all UTXOs are used and set to 0? What happens if I make a new remittance?

Create a new benchmarking process, Load wallet dump into it. 
"""
from curses import raw
import threading
import asyncio
import json
import random
import os
import decimal
import zipfile
import shutil

from test_framework.mininode import NetworkThread
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, wait_until, get_datadir_path, initialize_datadir, sync_blocks
from test_framework.blocktools import findTPC, create_colored_transaction, TOKEN_TYPES


decimal.getcontext().prec = 4

class BlockGenertorThread(threading.Thread):
    block_event_loop = None

    def __init__(self, maxBlockCount, node, signblockprivkey_wif):
        super().__init__(name="BlockGenertorThread")
        # There is only one event loop and no more than one thread must be created
        assert not self.block_event_loop

        BlockGenertorThread.block_event_loop = asyncio.get_event_loop()
        self.maxBlockCount = int(maxBlockCount)
        self.node = node
        self.signblockprivkey_wif = signblockprivkey_wif

    def run(self):
        """Schedule block generation."""
        t = BlockGenertorThread.block_event_loop.create_task(self.generate())
        #BlockGenertorThread.block_event_loop.run_forever()

    def close(self, timeout=10):
        """Close the block generation event loop."""
        self.block_event_loop.call_soon_threadsafe(self.block_event_loop.stop)
        wait_until(lambda: not self.block_event_loop.is_running(), timeout=timeout)
        self.block_event_loop.close()
        self.join(timeout)

    async def generate(self):
        while True:
            BlockGenertorThread.block_event_loop.call_soon_threadsafe(self.node.generate, 1, self.signblockprivkey_wif)
            print("%s - %s" %(self.node.getblockcount(), self.node.getbestblockhash()))
            await asyncio.sleep(1)


class Tapyrux20kWalletTest(BitcoinTestFramework):
    templates = []

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = False
        self.extra_args=[["-reindex", "-rescan"]]

    def setup_chain(self):
        # since we are using the data directory that is generated already
        # we skip chain setup
        datadir = get_datadir_path(self.options.tmpdir, 0)
        with zipfile.ZipFile(os.path.join(os.path.dirname(os.path.realpath(__file__)), "data/node0.zip"), 'r') as walletarchive:
            walletarchive.extractall(datadir)
        #port_seed = random.randint(1, MAX_NODES)
        initialize_datadir(self.options.tmpdir, 0)
        print("Data dir: %s" % datadir)
        #return super().setup_chain()

    def loadTxtemplates(self):
        with open(os.path.join(os.path.dirname(os.path.realpath(__file__)), 'data/templates.json'), encoding='utf-8') as f:
            jsonstr = json.load(f)
            self.templates = jsonstr['txs']

    def templateToTx(self, num, unspent):
        if num > len(self.templates):
            num = num % len(self.templates)

        txTemplate = self.templates[num]

        inputs = []
        outputs = []
        amt = {}
        amt['TPC'] = 0
        for x in range(0, txTemplate['in']):
            utxo = unspent[0]
            if(utxo['token'] not in amt.keys()):
                amt[ utxo['token'] ] = 0
            amt[ utxo['token'] ] += utxo['amount']
            inputs.append({'txid' : utxo['txid'], 'vout' : utxo['vout']})
            unspent = unspent[1:]
        for x in range(0, txTemplate['out']):
            outamt = (amt[ utxo['token'] ] - decimal.Decimal(0.05)) /txTemplate['out']
            outputs.append({self.nodes[0].getnewaddress() : outamt })
        #print("in :%s\n out : %s" % (inputs, outputs))
        return (inputs, outputs)

    async def generatetx(self, spend=False):
        unspent = self.nodes[0].listunspent()
        cnt = len(unspent)
        print(cnt)
        while cnt > 0 and ((not spend and cnt < int(self.options.maxUtxoCount) or spend and cnt > int(self.options.maxUtxoCount)/2)):
            if spend:
                num = random.randint(0, 3)
            else:
                num = random.randint(0, len(self.templates)-1)
            (inputs, outputs) = self.templateToTx(num, unspent)
            try:
                raw_tx =  self.nodes[0].createrawtransaction(inputs, outputs)
                signed_tx =  self.nodes[0].signrawtransactionwithwallet(raw_tx)
                self.nodes[0].sendrawtransaction(signed_tx['hex'], True)
            except:
                print("exception ignored")
            await asyncio.sleep(2)
            unspent = self.nodes[0].listunspent()
            cnt = len(unspent)
            await asyncio.sleep(0)
            print(cnt)
        loop = asyncio.get_event_loop()
        loop.stop()

    def run_test(self):
        self.log.info("Load wallet")
        self.nodes[0].importprivkey(self.signblockprivkey_wif)
        self.nodes[0].importwallet(os.path.join(os.path.dirname(os.path.realpath(__file__)), "data/wallet20000utxo"))

        # Needs rescan
        self.nodes[0].stop()
        self.nodes[0].start()

        self.log.info("Listunspent 1:%d" %len(self.nodes[0].listunspent()))
        self.nodes[0].getbalance()
        self.block_generator = BlockGenertorThread(1000, self.nodes[0], self.signblockprivkey_wif)
        self.block_generator.start()

        self.log.info("Starting transaction generator")
        self.loadTxtemplates()
        loop = asyncio.get_event_loop()
        loop.create_task(self.generatetx())
        self.block_generator.join()
        loop.run_forever()


if __name__ == '__main__':
    Tapyrux20kWalletTest().main()

