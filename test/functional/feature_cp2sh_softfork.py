#!/usr/bin/env python3
# Copyright (c) 2024 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
CP2SH colored coin softfork activation test.

Verifies that SCRIPT_VERIFY_CP2SH_COLORED correctly gates redeem-script
evaluation for colored P2SH outputs:

  Before activation height
    Only the OP_HASH160 equality check is enforced.  A spending transaction
    whose scriptSig pushes a redeemScript whose hash matches but whose body
    evaluates to false (OP_0) is accepted.

  At and after activation height
    The redeemScript must also evaluate to true.  The same transaction is
    rejected; a spend with OP_1 (truthy) is accepted.

Requires CP2SH_ACTIVATION_TEST_HEIGHT=120 to be defined at compile time
(set by the CI build) so the activation boundary can be crossed without
mining hundreds of thousands of blocks.
"""

import struct

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import (
    COIN, COutPoint, CTransaction, CTxIn, CTxOut, FromHex, ToHex, sha256,
)
from test_framework.script import (
    CScript, OP_0, OP_1, OP_COLOR, OP_EQUAL, OP_HASH160, OP_RETURN,
    hash160,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, bytes_to_hex_str, hex_str_to_bytes

# Activation height used throughout the test.  Low enough to reach quickly.
ACTIVATION_HEIGHT = 120

NON_REISSUABLE = 0xc2


def _color_id(txid_hex, vout):
    """NON_REISSUABLE colorId: 0xc2 || sha256(outpoint_le)."""
    txid_le = hex_str_to_bytes(txid_hex)[::-1]
    return bytes([NON_REISSUABLE]) + sha256(txid_le + struct.pack('<I', vout))


def _cp2sh_colored_spk(colorid_bytes, redeem_bytes):
    """OP_COLOR <colorid> OP_HASH160 <hash160(redeemScript)> OP_EQUAL"""
    return CScript([colorid_bytes, OP_COLOR, OP_HASH160, hash160(redeem_bytes), OP_EQUAL])


class CP2SHSoftforkTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    # ------------------------------------------------------------------ helpers

    def _wrap_submit(self, *txs, expect_reject=None):
        """Wrap one or more transactions in a block and call submitblock."""
        node = self.nodes[0]
        tip = node.getbestblockhash()
        height = node.getblockcount() + 1
        cb = create_coinbase(height)
        blk = create_block(int(tip, 16), cb, node.getblock(tip)['time'] + 1)
        for tx in txs:
            tx.rehash()
            blk.vtx.append(tx)
        blk.hashMerkleRoot = blk.calc_merkle_root()
        blk.hashImMerkleRoot = blk.calc_immutable_merkle_root()
        blk.solve(self.signblockprivkey)
        result = node.submitblock(bytes_to_hex_str(blk.serialize()))
        if expect_reject is None:
            assert result is None, "Block unexpectedly rejected: %s" % result
        else:
            assert expect_reject in (result or ''), \
                "Expected '%s' in submitblock result, got: %s" % (expect_reject, result)

    def _issue_cp2sh(self, utxo, redeem_bytes):
        """
        Build and wallet-sign a NON_REISSUABLE issuance that creates a single
        CP2SH colored output (100 satoshis / 100 tokens).

        Returns (CTransaction, colorid_bytes).  The wallet signs only the TPC
        input; the colored output needs no signature at creation time.
        """
        node = self.nodes[0]
        cid = _color_id(utxo['txid'], utxo['vout'])
        spk = _cp2sh_colored_spk(cid, redeem_bytes)
        fee = 10_000
        change = int(utxo['amount'] * COIN) - 100 - fee

        tx = CTransaction()
        tx.nFeatures = 1
        tx.vin.append(CTxIn(COutPoint(int(utxo['txid'], 16), utxo['vout'])))
        tx.vout.append(CTxOut(100, spk))
        tx.vout.append(CTxOut(change, CScript(hex_str_to_bytes(utxo['scriptPubKey']))))

        signed = node.signrawtransactionwithwallet(ToHex(tx))
        assert signed['complete'], "Signing failed: %s" % signed.get('errors')
        return FromHex(CTransaction(), signed['hex']), cid

    def _spend_cp2sh(self, issue_tx, vout, redeem_bytes, tpc_utxo):
        """
        Build a spending transaction for a CP2SH colored UTXO.

        Includes a TPC fee input (tpc_utxo) — VerifyTokenBalances requires
        tpcin > 0 for any transaction that moves colored coins.

        scriptSig for the colored input = push(redeemScript bytes only):
          pre-activation  — hash matches → accepted (no redeem-script eval)
          post-activation — hash matches then redeemScript is evaluated;
                            OP_0 (falsy) will cause rejection, OP_1 passes.

        The colored tokens are burned (OP_RETURN output with 0 value).
        """
        node = self.nodes[0]
        fee = 10_000
        change = int(tpc_utxo['amount'] * COIN) - fee

        tx = CTransaction()
        tx.nFeatures = 1
        # Input 0: the CP2SH colored coin (scriptSig = push(redeemScript))
        tx.vin.append(CTxIn(
            COutPoint(int(issue_tx.hashMalFix, 16), vout),
            CScript([redeem_bytes]),
        ))
        # Input 1: TPC input to satisfy the fee requirement
        tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'], 16), tpc_utxo['vout'])))
        # Output 0: burn the colored tokens
        tx.vout.append(CTxOut(0, CScript([OP_RETURN])))
        # Output 1: TPC change
        tx.vout.append(CTxOut(change, CScript(hex_str_to_bytes(tpc_utxo['scriptPubKey']))))

        # Let the wallet sign the TPC input; it will skip the colored input.
        signed = node.signrawtransactionwithwallet(ToHex(tx))
        tx = FromHex(CTransaction(), signed['hex'])
        # Restore the colored input's scriptSig (wallet may have cleared it).
        tx.vin[0].scriptSig = CScript([redeem_bytes])
        return tx

    # ------------------------------------------------------------------ test

    def run_test(self):
        node = self.nodes[0]

        # Mine enough blocks for coinbase maturity.
        node.generate(101, self.signblockprivkey_wif)
        assert_equal(node.getblockcount(), 101)

        # Two distinct TPC UTXOs for the two issuance transactions.
        tpc_utxos = sorted(
            [u for u in node.listunspent() if u.get('token') == 'TPC'],
            key=lambda u: u['amount'], reverse=True,
        )

        # redeemScript that evaluates to false: OP_0 pushes [] (falsy).
        bad_redeem  = bytes(CScript([OP_0]))   # b'\x00'
        # redeemScript that evaluates to true: OP_1 pushes 1 (truthy).
        good_redeem = bytes(CScript([OP_1]))   # b'\x51'

        # ── pre-activation ──────────────────────────────────────────────────
        # Issue a CP2SH colored output with a bad redeemScript (OP_0).
        # Spending it before the activation height must succeed because only
        # the hash comparison is enforced.

        self.log.info("Pre-activation: spend with OP_0 redeemScript accepted (hash-match only)")

        issue_pre, _ = self._issue_cp2sh(tpc_utxos[0], bad_redeem)
        self._wrap_submit(issue_pre)                          # block 102
        issue_pre.rehash()

        # Get a fresh TPC UTXO (change from issue_pre is now confirmed).
        fee_utxo_pre = sorted(
            [u for u in node.listunspent() if u.get('token') == 'TPC'],
            key=lambda u: u['amount'], reverse=True,
        )[0]
        spend_pre = self._spend_cp2sh(issue_pre, 0, bad_redeem, fee_utxo_pre)
        self._wrap_submit(spend_pre)                          # block 103 — must succeed
        self.log.info("  ✓ accepted at block %d" % node.getblockcount())

        # ── advance to activation height - 1 ────────────────────────────────
        blocks_needed = ACTIVATION_HEIGHT - 1 - node.getblockcount()
        if blocks_needed > 0:
            node.generate(blocks_needed, self.signblockprivkey_wif)
        assert_equal(node.getblockcount(), ACTIVATION_HEIGHT - 1)

        # ── issue post-activation UTXOs at the activation block ─────────────
        # Get fresh UTXOs (earlier ones may be spent or confirmed as change).
        tpc_utxos_post = sorted(
            [u for u in node.listunspent() if u.get('token') == 'TPC'],
            key=lambda u: u['amount'], reverse=True,
        )
        issue_bad,  _ = self._issue_cp2sh(tpc_utxos_post[0], bad_redeem)
        issue_good, _ = self._issue_cp2sh(tpc_utxos_post[1], good_redeem)

        # Both issuances go in block ACTIVATION_HEIGHT.
        self._wrap_submit(issue_bad, issue_good)
        assert_equal(node.getblockcount(), ACTIVATION_HEIGHT)
        issue_bad.rehash(); issue_good.rehash()
        self.log.info("Issued bad/good CP2SH UTXOs at activation block %d" % ACTIVATION_HEIGHT)

        # ── post-activation: bad redeemScript rejected ───────────────────────
        # Get fresh TPC UTXOs for the post-activation spends.
        fee_utxos_post = sorted(
            [u for u in node.listunspent() if u.get('token') == 'TPC'],
            key=lambda u: u['amount'], reverse=True,
        )
        self.log.info("Post-activation: spend with OP_0 redeemScript rejected")
        spend_bad = self._spend_cp2sh(issue_bad, 0, bad_redeem, fee_utxos_post[0])
        self._wrap_submit(spend_bad, expect_reject="block-validation-failed")
        self.log.info("  ✓ rejected (chain stays at block %d)" % node.getblockcount())

        # ── post-activation: valid redeemScript accepted ─────────────────────
        # spend_bad's block was rejected so fee_utxos_post[0] is still unspent;
        # use a different UTXO for spend_good to avoid any double-spend risk.
        self.log.info("Post-activation: spend with OP_1 redeemScript accepted")
        spend_good = self._spend_cp2sh(issue_good, 0, good_redeem, fee_utxos_post[1])
        self._wrap_submit(spend_good)                         # must succeed
        self.log.info("  ✓ accepted at block %d" % node.getblockcount())


if __name__ == '__main__':
    CP2SHSoftforkTest().main()
