#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Stress-test PSTT construction at scale: large and very-large PSTTs (tens
of inputs each), assembled via joinpstt (see doc/tapyrus/pstt.md's
Constructor role) rather than one addinputtopstt call per input, then signed,
finalized, and mined -- checking that multiple such transactions can fill a
single block, and that a larger batch of them needs to span several blocks.

Runs against a single node with a small -blockmaxsize so this is exercisable
without building actual near-1MB transactions (which would only make the
test slower, not exercise anything -blockmaxsize itself doesn't already
cover) -- only the transaction *count* and relative *size* matter here, not
real TPC value, so every input below is simply a whole, distinct coinbase
UTXO from this node's own wallet.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

# Chosen with several hundred bytes of headroom on both sides of the fit/
# no-fit boundary, since a P2PKH scriptSig's exact size varies by a couple of
# bytes per input with DER signature encoding -- not a razor-thin fit that
# would make this test flaky.
BLOCK_MAX_SIZE = 10000

# Two of these fill one block together (under BLOCK_MAX_SIZE); see
# test_large_ptts_fill_one_block.
LARGE_INPUT_COUNT = 28

# Built via joinpstt out of this many separately-constructed pieces; the
# result is already too big to share a block with another one like it, so
# several of them each need their own block. See
# test_very_large_pstts_span_blocks.
VERY_LARGE_PIECE_COUNT = 3
VERY_LARGE_INPUTS_PER_PIECE = 15
VERY_LARGE_INPUT_COUNT = VERY_LARGE_PIECE_COUNT * VERY_LARGE_INPUTS_PER_PIECE
VERY_LARGE_PSTT_COUNT = 3

FEE = Decimal('0.01')


class PSTTLargeScaleTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [[f"-blockmaxsize={BLOCK_MAX_SIZE}"]]

    def run_test(self):
        node = self.nodes[0]
        total_utxos_needed = 2 * LARGE_INPUT_COUNT + VERY_LARGE_PSTT_COUNT * VERY_LARGE_INPUT_COUNT
        # A distinct coinbase UTXO per block, plus a little headroom.
        node.generate(total_utxos_needed + 5, self.signblockprivkey_wif)

        self.test_large_ptts_fill_one_block()
        self.test_very_large_pstts_span_blocks()

    def _spend_utxos_to_one_output(self, utxos):
        """Builds, signs, and finalizes (but does not broadcast) a PSTT
        spending every one of the given wallet UTXOs into a single
        new-address output, via one createpstt call -- its own inputs array
        already accepts however many are given, no addinputtopstt looping
        needed for a single-party build like this one."""
        node = self.nodes[0]
        total = sum(u['amount'] for u in utxos)
        pstt = node.createpstt(
            [{"txid": u['txid'], "vout": u['vout']} for u in utxos],
            {node.getnewaddress(): total - FEE},
        )
        processed = node.walletprocesspstt(pstt)
        assert_equal(processed['complete'], True)
        assert_equal(node.decodepstt(processed['pstt'])['next'], 'extractor')  # single-key P2PKH: walletprocesspstt already fully finalizes
        return node.finalizepstt(processed['pstt'])['hex']

    def _build_very_large_pstt(self, utxos):
        """Builds one VERY_LARGE_INPUT_COUNT-input PSTT out of
        VERY_LARGE_PIECE_COUNT separately-constructed pieces via joinpstt --
        the scenario joinpstt exists for (see doc/tapyrus/pstt.md's
        Constructor role): assembling a large PSTT without a single caller
        driving one addinputtopstt call per input. Signs, finalizes, but
        does not broadcast."""
        node = self.nodes[0]
        assert_equal(len(utxos), VERY_LARGE_INPUT_COUNT)

        pieces = []
        for i in range(VERY_LARGE_PIECE_COUNT):
            chunk = utxos[i * VERY_LARGE_INPUTS_PER_PIECE:(i + 1) * VERY_LARGE_INPUTS_PER_PIECE]
            pieces.append(node.createpstt(
                [{"txid": u['txid'], "vout": u['vout']} for u in chunk],
                [], 0, True, True,
            ))
        joined = node.joinpstt(pieces)
        decoded_joined = node.decodepstt(joined)
        assert_equal(len(decoded_joined['inputs']), VERY_LARGE_INPUT_COUNT)
        assert_equal(len(decoded_joined['outputs']), 0)

        total = sum(u['amount'] for u in utxos)
        with_output = node.addoutputtopstt(joined, {node.getnewaddress(): total - FEE})
        finished = node.finalizepsttconstruction(with_output)

        processed = node.walletprocesspstt(finished)
        assert_equal(processed['complete'], True)
        assert_equal(node.decodepstt(processed['pstt'])['next'], 'extractor')  # single-key P2PKH: walletprocesspstt already fully finalizes
        return node.finalizepstt(processed['pstt'])['hex']

    def test_large_ptts_fill_one_block(self):
        self.log.info("Test: multiple large PSTTs (%d inputs each) filling one block" % LARGE_INPUT_COUNT)
        node = self.nodes[0]
        unspent = node.listunspent()
        assert len(unspent) >= 2 * LARGE_INPUT_COUNT, "not enough spendable UTXOs for this scenario"

        txids = []
        for i in range(2):
            chunk = unspent[i * LARGE_INPUT_COUNT:(i + 1) * LARGE_INPUT_COUNT]
            hex_tx = self._spend_utxos_to_one_output(chunk)
            txids.append(node.sendrawtransaction(hex_tx, True))

        assert_equal(node.getmempoolinfo()['size'], 2)
        total_bytes = sum(node.getrawtransaction(t, True)['size'] for t in txids)

        blockhash = node.generate(1, self.signblockprivkey_wif)[0]
        assert_equal(node.getmempoolinfo()['size'], 0)
        block = node.getblock(blockhash)
        for txid in txids:
            assert txid in block['tx']
        self.log.info("    both large PSTTs (%d bytes total) confirmed together in one block" % total_bytes)

    def test_very_large_pstts_span_blocks(self):
        self.log.info("Test: very-large PSTTs (%d inputs each, built via joinpstt) spanning multiple blocks" % VERY_LARGE_INPUT_COUNT)
        node = self.nodes[0]
        unspent = node.listunspent()
        assert len(unspent) >= VERY_LARGE_PSTT_COUNT * VERY_LARGE_INPUT_COUNT, "not enough spendable UTXOs for this scenario"

        txids = []
        for i in range(VERY_LARGE_PSTT_COUNT):
            chunk = unspent[i * VERY_LARGE_INPUT_COUNT:(i + 1) * VERY_LARGE_INPUT_COUNT]
            hex_tx = self._build_very_large_pstt(chunk)
            txids.append(node.sendrawtransaction(hex_tx, True))

        assert_equal(node.getmempoolinfo()['size'], VERY_LARGE_PSTT_COUNT)

        # Each very-large PSTT was sized (via BLOCK_MAX_SIZE/joinpstt above)
        # to need its own block, so clearing all of them must take exactly
        # VERY_LARGE_PSTT_COUNT blocks -- never fewer (would mean two shared
        # a block, undermining the "very-large" sizing) and never more
        # (would mean mining stalled on one of them).
        blocks_mined = 0
        while node.getmempoolinfo()['size'] > 0:
            node.generate(1, self.signblockprivkey_wif)
            blocks_mined += 1
            assert blocks_mined <= VERY_LARGE_PSTT_COUNT, "mining stalled -- a very-large PSTT should always fit in its own block"

        assert_equal(blocks_mined, VERY_LARGE_PSTT_COUNT)
        self.log.info("    %d very-large PSTTs required %d separate blocks to confirm" % (VERY_LARGE_PSTT_COUNT, blocks_mined))


if __name__ == '__main__':
    PSTTLargeScaleTest().main()
