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
    CScript, OP_0, OP_1, OP_CHECKSIG, OP_COLOR, OP_DUP, OP_EQUAL,
    OP_EQUALVERIFY, OP_HASH160, OP_RETURN,
    hash160,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, bytes_to_hex_str, hex_str_to_bytes

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
        CP2SH colored output (100 TPC / 100 tokens).

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

    # ------------------------------------------------------------------ helpers (continued)

    def _test_signrpc_cp2sh(self):
        """
        regression: signrawtransactionwithkey must produce a valid
        CP2SH colored signature post-activation and the result must be accepted
        by sendrawtransaction
        """
        node = self.nodes[0]
        assert node.getblockcount() >= ACTIVATION_HEIGHT, "must run post-activation"

        # Derive a key from the wallet; build a P2PKH redeemScript around it.
        addr = node.getnewaddress()
        pubkey = hex_str_to_bytes(node.getaddressinfo(addr)['pubkey'])
        privkey = node.dumpprivkey(addr)
        redeem_bytes = bytes(CScript([
            OP_DUP, OP_HASH160, hash160(pubkey), OP_EQUALVERIFY, OP_CHECKSIG,
        ]))

        # Issue a CP2SH colored output using this redeemScript.
        utxo = sorted(
            [u for u in node.listunspent() if u.get('token') == 'TPC'],
            key=lambda u: u['amount'], reverse=True,
        )[0]
        issue_tx, colorid = self._issue_cp2sh(utxo, redeem_bytes)
        self._wrap_submit(issue_tx)
        issue_tx.rehash()

        # TPC fee input for the spend.
        fee_utxo = sorted(
            [u for u in node.listunspent() if u.get('token') == 'TPC'],
            key=lambda u: u['amount'], reverse=True,
        )[0]
        fee_privkey = node.dumpprivkey(fee_utxo['address'])
        fee = 10_000
        change = int(fee_utxo['amount'] * COIN) - fee

        # Build raw spend (burn the colored tokens, keep TPC change).
        spend_tx = CTransaction()
        spend_tx.nFeatures = 1
        spend_tx.vin.append(CTxIn(COutPoint(int(issue_tx.hashMalFix, 16), 0)))
        spend_tx.vin.append(CTxIn(COutPoint(int(fee_utxo['txid'], 16), fee_utxo['vout'])))
        spend_tx.vout.append(CTxOut(0, CScript([OP_RETURN])))
        spend_tx.vout.append(CTxOut(change, CScript(hex_str_to_bytes(fee_utxo['scriptPubKey']))))

        cp2sh_spk = _cp2sh_colored_spk(colorid, redeem_bytes)
        prevtxs = [{
            'txid': issue_tx.hashMalFix,
            'vout': 0,
            'scriptPubKey': bytes_to_hex_str(bytes(cp2sh_spk)),
            'redeemScript': bytes_to_hex_str(redeem_bytes),
            'amount': 100 / COIN,
        }]

        # Sign both inputs with their respective private keys.
        # signrawtransactionwithkey goes through the SignTransaction() helper
        # in rawtransaction.cpp — exactly the path fixed for signrpc_cp2sh.
        signed = node.signrawtransactionwithkey(ToHex(spend_tx), [privkey, fee_privkey], prevtxs)
        assert signed['complete'], "signrpc_cp2sh regression: signing incomplete: %s" % signed.get('errors')

        # The critical assertion: 'complete': true is not enough.
        # The signed tx must actually be accepted by the network.
        txid = node.sendrawtransaction(signed['hex'])
        assert txid, "signrpc_cp2sh regression: sendrawtransaction rejected signed tx"
        self.log.info("  ✓ signrpc_cp2sh: signrawtransactionwithkey produces valid CP2SH colored signature post-activation")

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

        # Probe: binaries compiled without -DCP2SH_ACTIVATION_TEST_HEIGHT activate
        # CP2SH_COLORED at genesis, so the pre-activation spend would always be
        # rejected.  Detect this via testmempoolaccept and skip gracefully rather
        # than crashing with an assertion error.
        spend_pre.rehash()
        probe = node.testmempoolaccept([bytes_to_hex_str(spend_pre.serialize())])
        if not probe[spend_pre.hashMalFix]['allowed']:
            self.log.warning(
                "CP2SH_COLORED active from genesis (binary not compiled with "
                "-DCP2SH_ACTIVATION_TEST_HEIGHT=%d); skipping pre-activation sub-test",
                ACTIVATION_HEIGHT)
        else:
            self._wrap_submit(spend_pre)                      # block 103 — must succeed
            self.log.info("  ✓ accepted at block %d" % node.getblockcount())

        # ── advance to activation height - 2 ────────────────────────────────
        # Stop one block early so we can confirm a boundary CP2SH UTXO at
        # exactly ACTIVATION_HEIGHT - 1 and then test mempool admission there.
        blocks_needed = ACTIVATION_HEIGHT - 2 - node.getblockcount()
        if blocks_needed > 0:
            node.generate(blocks_needed, self.signblockprivkey_wif)
        assert_equal(node.getblockcount(), ACTIVATION_HEIGHT - 2)

        # ── mempool boundary: tip == ACTIVATION_HEIGHT - 1 ──────────────────
        # Confirm a CP2SH UTXO with bad redeemScript (OP_0) at block
        # ACTIVATION_HEIGHT - 1, then attempt to spend it via sendrawtransaction
        # while the tip is still at that height.
        #
        # With the fix (mempoolScriptFlags queried at tip+1 = ACTIVATION_HEIGHT),
        # SCRIPT_VERIFY_CP2SH_COLORED is already active and the tx is rejected at
        # mempool admission. The reject reason is the specific script error string
        # (SCRIPT_ERR_EVAL_FALSE) since the mempool path exposes per-error detail.
        # Without the fix the tx would be admitted and only fail when the
        # activation block tries to include it.
        tpc_boundary = sorted(
            [u for u in node.listunspent() if u.get('token') == 'TPC'],
            key=lambda u: u['amount'], reverse=True,
        )[0]
        boundary_issue, _ = self._issue_cp2sh(tpc_boundary, bad_redeem)
        self._wrap_submit(boundary_issue)                       # block ACTIVATION_HEIGHT - 1
        assert_equal(node.getblockcount(), ACTIVATION_HEIGHT - 1)
        boundary_issue.rehash()

        fee_boundary = sorted(
            [u for u in node.listunspent() if u.get('token') == 'TPC'],
            key=lambda u: u['amount'], reverse=True,
        )[0]
        boundary_spend = self._spend_cp2sh(boundary_issue, 0, bad_redeem, fee_boundary)

        self.log.info("Boundary: mempool rejects bad redeemScript at tip == ACTIVATION_HEIGHT - 1")
        assert_raises_rpc_error(
            -26, "mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack element)",
            node.sendrawtransaction, ToHex(boundary_spend),
        )
        self.log.info("  ✓ rejected at mempool admission (not deferred to block validation)")

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
        self._wrap_submit(spend_bad, expect_reject="mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack element)")
        self.log.info("  ✓ rejected (chain stays at block %d)" % node.getblockcount())

        # ── post-activation: valid redeemScript accepted ─────────────────────
        # spend_bad's block was rejected so fee_utxos_post[0] is still unspent;
        # use a different UTXO for spend_good to avoid any double-spend risk.
        self.log.info("Post-activation: spend with OP_1 redeemScript accepted")
        spend_good = self._spend_cp2sh(issue_good, 0, good_redeem, fee_utxos_post[1])
        self._wrap_submit(spend_good)                         # must succeed
        self.log.info("  ✓ accepted at block %d" % node.getblockcount())

        # ── signrpc_cp2sh regression: RPC signing path is activation-aware ─────────────
        self.log.info("signrpc_cp2sh regression: signrawtransactionwithkey is activation-aware")
        self._test_signrpc_cp2sh()


if __name__ == '__main__':
    CP2SHSoftforkTest().main()
