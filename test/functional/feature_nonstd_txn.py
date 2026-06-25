#!/usr/bin/env python3
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test non-standard transaction policy (mempool) vs consensus (block) behavior.

Two nodes run independently with different configurations:
- node 0 (standard): default node, rejects non-standard transactions from mempool
- node 1 (debug, -acceptnonstdtxn=1): accepts non-standard transactions into mempool

Tests:
1. testmempoolaccept: standard node rejects, non-standard node accepts
2. Block acceptance: both nodes accept a block containing a non-standard transaction
   (consensus rules do not enforce standardness)

Note: -acceptnonstdtxn=1 is only available in debug builds.
"""
from io import BytesIO

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    ToHex,
)
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, bytes_to_hex_str, hex_str_to_bytes


class NonStdTxnTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            [],                      # node 0: standard, mempool rejects non-std txns
            ['-acceptnonstdtxn=1'],  # node 1: debug only, mempool accepts non-std txns
        ]

    def setup_network(self):
        # Nodes operate independently; no p2p connections between them.
        self.setup_nodes()

    def run_test(self):
        node0 = self.nodes[0]  # standard node
        node1 = self.nodes[1]  # non-standard node (-acceptnonstdtxn=1)

        # Mine blocks on each node independently to get wallet funds.
        node0.generate(10, self.signblockprivkey_wif)
        node1.generate(10, self.signblockprivkey_wif)

        nonstd_hex0 = self._create_nonstd_tx_hex(node0)
        nonstd_hex1 = self._create_nonstd_tx_hex(node1)

        self.log.info("Standard node rejects non-standard tx from mempool")
        result = node0.testmempoolaccept([nonstd_hex0])
        assert_equal(list(result.values())[0]['allowed'], False)

        self.log.info("Non-standard node (-acceptnonstdtxn=1) accepts non-standard tx in mempool")
        result = node1.testmempoolaccept([nonstd_hex1])
        assert_equal(list(result.values())[0]['allowed'], True)
        txid = node1.sendrawtransaction(nonstd_hex1)
        assert txid in node1.getrawmempool()

        self.log.info("Standard node accepts a block with non-standard tx via submitblock (consensus)")
        tx0 = CTransaction()
        tx0.deserialize(BytesIO(hex_str_to_bytes(nonstd_hex0)))
        tx0.rehash()
        self._submit_block_with_tx(node0, tx0)

        self.log.info("Non-standard node mines a block; non-standard tx from mempool is included")
        blockhash = node1.generate(1, self.signblockprivkey_wif)[0]
        assert txid in node1.getblock(blockhash)['tx']

    def _create_nonstd_tx_hex(self, node):
        """Return a wallet-signed raw tx hex with a bare OP_TRUE output (non-standard)."""
        utxo = node.listunspent()[0]

        # Build tx: spend wallet UTXO → bare OP_TRUE output
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(utxo['txid'], 16), utxo['vout'])))
        tx.vout.append(CTxOut(int(utxo['amount'] * COIN) - 1000, CScript([OP_TRUE])))

        # Sign the input with the wallet (wallet owns the input; output script doesn't matter)
        signed = node.signrawtransactionwithwallet(ToHex(tx), [], 'ALL', self.options.scheme)
        assert signed['complete']
        return signed['hex']

    def _submit_block_with_tx(self, node, tx):
        """Craft a block containing tx and submit it via RPC, asserting it is accepted."""
        tip = node.getbestblockhash()
        height = node.getblockcount() + 1
        block_time = node.getblockheader(tip)['mediantime'] + 1
        coinbase = create_coinbase(height)
        coinbase.rehash()
        block = create_block(int(tip, 16), coinbase, block_time)
        block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.solve(self.signblockprivkey)
        node.submitblock(bytes_to_hex_str(block.serialize()))
        assert_equal(node.getbestblockhash(), block.hash)


if __name__ == '__main__':
    NonStdTxnTest().main()
