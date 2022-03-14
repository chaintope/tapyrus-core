#!/usr/bin/env python3
# Copyright (c) 2014-2018 The Bitcoin Core developers
# Copyright (c) 2021 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
An independent python process that can
    - generate transactions on a tapyrus node in real time.
    - or generate a blockchain or wallet for performance test

Transactions are created from the templates in data/templates.json. We pick one of them randomly and fill it with utxos from the blockchain to create a new transaction.

In daemon mode this process can be used to fill the blocks in the testnet:
    Transactions may be valid or invalid. Frequency of transaction is configurable.

In benchmark mode it is used to create a wallet with maxUtxoCount(20000) utxos.
* This means that maxUtxoCount(20,000) UTXOs are returned in listunspent.
* How long does it take to execute this command?
* If maxUtxoCount(10,000) of these are used and UTXO is reduced to maxUtxoCount(10,000), what will happen to the execution time of listunspent?
* What happens to the listunspent execution time if all UTXOs are used and set to 0? What happens if I make a new remittance?

"""
from curses import raw
import threading
import asyncio
import json
import random
import os
import decimal
import shutil
from test_framework.mininode import NetworkThread
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, wait_until
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
            await asyncio.sleep(2)


class TapyruxTxGenerator(BitcoinTestFramework):
    templates = []

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

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
            utxo = unspent[random.randint(0, len(unspent)-1)]
            if( int(utxo['amount']) - decimal.Decimal(0.05) < 0):
                return (None, None)
            if(utxo['token'] not in amt.keys()):
                amt[ utxo['token'] ] = 0
            amt[ utxo['token'] ] =  amt[ utxo['token'] ] + utxo['amount']
            inputs.append({'txid' : utxo['txid'], 'vout' : utxo['vout']})
            unspent = unspent[1:]

        if (amt[ utxo['token'] ] - decimal.Decimal(0.05))/txTemplate['out'] < 0:
            return (None, None)
        for x in range(0, txTemplate['out']):
            outamt = (amt[ utxo['token'] ] - decimal.Decimal(0.05)) /txTemplate['out']
            outputs.append({self.nodes[0].getnewaddress() : outamt })
        #print("in :%s\n out : %s" % (inputs, outputs))
        return (inputs, outputs)

    async def generatetx(self):
        unspent = self.nodes[0].listunspent()
        cnt = len(unspent)
        while cnt > 0 and cnt < int(self.options.maxUtxoCount):
            num = random.randint(0, len(self.templates)-1)
            (inputs, outputs) = self.templateToTx(num, unspent)
            if inputs is None:
                continue
            try:
                raw_tx =  self.nodes[0].createrawtransaction(inputs, outputs)
                signed_tx =  self.nodes[0].signrawtransactionwithwallet(raw_tx)
                self.nodes[0].sendrawtransaction(signed_tx['hex'], True)
            except:
                print("exception ignored")#:\n%s" % traceback.print_exc())
            await asyncio.sleep(0)
            unspent = self.nodes[0].listunspent()
            cnt = len(unspent)
            print(cnt)
        loop = asyncio.get_event_loop()
        loop.stop()

    def run_test(self):
        self.log.info("Starting block generator")
        self.nodes[0].importprivkey(self.signblockprivkey_wif)
        self.nodes[0].generate(100, self.signblockprivkey_wif)

        self.block_generator = BlockGenertorThread(self.options.maxBlockCount, self.nodes[0], self.signblockprivkey_wif)
        self.block_generator.start()

        self.log.info("Starting transaction generator")
        self.loadTxtemplates()
        loop = asyncio.get_event_loop()
        loop.create_task(self.generatetx())
        loop.create_task(self.generatetx())
        loop.create_task(self.generatetx())
        self.block_generator.join()
        loop.run_forever()
        self.nodes[0].dumpwallet(str(os.path.join(os.path.dirname(os.path.realpath(__file__)), str.format("wallet%sutxo" % self.options.maxUtxoCount))))
        shutil.make_archive(os.path.join(os.path.dirname(os.path.realpath(__file__)), "node0"), 'zip', os.path.join(self.options.tmpdir, "node0"))

    def add_options(self, parser):
        ''' options:
        1. - daemon mode(run forever)
                 or stopatcount mode(maxBlockCount=x or maxUtxoCount=x)
        2. maxBlockCount=x
        3. maxUtxoCount=x
        '''
        parser.add_argument("--daemon", dest="daemon", default=False, action="store_true",
                            help="Start in daemon mode")
        parser.add_argument("--maxBlockCount", dest="maxBlockCount", default=0,
                            help="Stop the node when maxBlockCount blocks are reached")
        parser.add_argument("--maxUtxoCount", dest="maxUtxoCount", default=0,
                            help="Stop the node when maxUtxoCount utxos are reached in the wallet")

if __name__ == '__main__':
    TapyruxTxGenerator().main()

