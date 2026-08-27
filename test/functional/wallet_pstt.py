#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the PSTT (TIP-174) Fee Provider workflow: walletfundpsttfee, both
the non-interactive and interactive variants (see the "Fee Provider
workflow" section of doc/tapyrus/pstt.md).

Both variants exist because fees in Tapyrus are payable only in TPC, so a
wallet holding only Colored Coins cannot complete a transaction alone: a
second party ("the fee provider") supplies the TPC, using only the
Constructor/Updater/Signer/Combiner primitives already covered by
rpc_pstt.py plus walletfundpsttfee itself -- the one RPC specific to this
workflow. This file drives that RPC and the two-party call sequence around
it; it is deliberately a separate file from rpc_pstt.py, since it's the
most complex end-to-end scenario in the whole PSTT surface.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, find_output
from test_framework.blocktools import create_colored_transaction


class PSTTFeeProviderTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def run_test(self):
        # Sync between these two generate() calls, not just after both --
        # node1 must have node0's block connected before mining its own, or
        # it races to build on the stale (genesis) tip: its own block loses
        # once node0's propagates, and its coinbase never confirms, making
        # node1's opening balance (used throughout test_noninteractive_success) zero
        # depending on timing.
        self.nodes[0].generate(1, self.signblockprivkey_wif)
        self.sync_all()
        self.nodes[1].generate(1, self.signblockprivkey_wif)
        self.sync_all()

        self.test_noninteractive_success()
        self.test_noninteractive_failure()
        self.test_interactive_success()
        self.test_interactive_failure()

    def test_noninteractive_success(self):
        self.log.info("Test walletfundpsttfee, noninteractive mode")
        node0 = self.nodes[0]  # token-only party
        node1 = self.nodes[1]  # TPC fee provider

        issued = create_colored_transaction(2, 100, node0)
        colorId = issued['color']
        issue_txid = issued['txid']
        node0.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        issue_vout = find_output(node0, issue_txid, 100)

        recipient_addr = node1.getnewaddress("feeprovider_recv", colorId)

        # User (token-only, primary path): build and sign the token side of
        # the transfer. Inputs-Modifiable stays set so the fee provider can
        # add a TPC input afterward; Outputs-Modifiable stays off, since the
        # transfer terms are already decided. Signing with
        # ALL|ANYONECANPAY is what makes adding more inputs later valid
        # without invalidating this signature.
        pstt = node0.createpstt(
            [{"txid": issue_txid, "vout": issue_vout}],
            {recipient_addr: 100},
            0, True, False)
        # walletsignpstt is the Signer role only -- it relies on a prior
        # Updater step (walletupdatepstt) having already attached each
        # input's PSTT_IN_UTXO; createpstt (Creator) never does that itself.
        pstt = node0.walletupdatepstt(pstt)['pstt']
        pstt = node0.walletsignpstt(pstt, "ALL|ANYONECANPAY", self.options.scheme)['pstt']
        decoded = node0.decodepstt(pstt)
        assert_equal(decoded['tx_modifiable']['inputs_modifiable'], True)

        # Provider: supplies one already-known TPC outpoint directly. Its
        # color is verified from its scriptPubKey -- walletfundpsttfee
        # rejects a colored-coin outpoint outright, never trusting the
        # caller's stated intent.
        fee_utxo = next(u for u in node1.listunspent() if u.get('token') == 'TPC')
        funded = node1.walletfundpsttfee(
            pstt, "noninteractive",
            {"txid": fee_utxo['txid'], "vout": fee_utxo['vout']})
        decoded_funded = node1.decodepstt(funded)
        assert_equal(len(decoded_funded['inputs']), 2)
        assert 'utxo' in decoded_funded['inputs'][1]

        # Provider signs its own new input with SIGHASH_ALL -- this closes
        # Inputs-Modifiable (a plain ALL signature doesn't tolerate
        # further inputs being added).
        signed = node1.walletsignpstt(funded, "ALL", self.options.scheme)
        assert_equal(signed['complete'], True)
        decoded_signed = node0.decodepstt(signed['pstt'])
        assert_equal(decoded_signed['tx_modifiable']['inputs_modifiable'], False)

        final_tx = node0.finalizepstt(signed['pstt'])['hex']
        node0.sendrawtransaction(final_tx, True)
        node0.generate(1, self.signblockprivkey_wif)
        self.sync_all()

        # The colored transfer landed on the recipient intact; the fee
        # UTXO's entire value was consumed as fee (noninteractive mode has
        # no change output -- see doc/tapyrus/pstt.md's "exact-fee UTXO
        # pool" description).
        decoded_final = node0.decoderawtransaction(final_tx)
        assert_equal(len(decoded_final['vout']), 1)
        assert_equal(decoded_final['vout'][0]['scriptPubKey']['addresses'][0], recipient_addr)

    def test_noninteractive_failure(self):
        # noninteractive mode never adds a paired output (see
        # test_noninteractive_success's "no change output" note above), so
        # walletfundpsttfee refuses outright -- before even looking at
        # fee_input -- once the PSTT already carries a SIGHASH_SINGLE
        # signature: a standalone input add is exactly what addinputtopstt
        # itself already refuses in that state (doc/tapyrus/pstt.md).
        self.log.info("Test walletfundpsttfee, noninteractive mode rejects "
                       "a SIGHASH_SINGLE PSTT")
        node0 = self.nodes[0]  # token-only party
        node1 = self.nodes[1]  # TPC fee provider

        # test_noninteractive_success spent node1's entire original coinbase
        # as a fee with no change -- top up before this scenario needs its
        # own TPC fee_input.
        node1.generate(1, self.signblockprivkey_wif)
        self.sync_all()

        issued = create_colored_transaction(2, 10, node0)
        colorId = issued['color']
        issue_txid = issued['txid']
        node0.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        issue_vout = find_output(node0, issue_txid, 10)

        recipient_addr = node1.getnewaddress("feeprovider_recv_noninteractive_fail", colorId)

        # has_sighash_single=True: no actual SIGHASH_SINGLE signature is
        # needed to exercise the guard -- createpstt sets
        # PSTT_TXMOD_HAS_SIGHASH_SINGLE directly from this flag, and
        # walletfundpsttfee's check only looks at that bit.
        pstt = node0.createpstt(
            [{"txid": issue_txid, "vout": issue_vout}],
            {recipient_addr: 10},
            0, True, False, True)

        fee_utxo = next(u for u in node1.listunspent() if u.get('token') == 'TPC')
        assert_raises_rpc_error(-8, "a standalone input add is not allowed",
            node1.walletfundpsttfee, pstt, "noninteractive",
            {"txid": fee_utxo['txid'], "vout": fee_utxo['vout']})

    def test_interactive_success(self):
        self.log.info("Test walletfundpsttfee, interactive mode")
        node0 = self.nodes[0]  # token-only party
        node1 = self.nodes[1]  # TPC fee provider

        # test_noninteractive_success spent node1's entire original coinbase
        # as a fee with no change (noninteractive mode has none) -- top up
        # before this scenario needs its own TPC to fund from.
        node1.generate(1, self.signblockprivkey_wif)
        self.sync_all()

        issued = create_colored_transaction(2, 50, node0)
        colorId = issued['color']
        issue_txid = issued['txid']
        node0.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        issue_vout = find_output(node0, issue_txid, 50)

        # The provider's own coin selection needs to know this input's value
        # to correctly recognize it as already covered, rather than trying
        # (and failing) to also source colored coins of its own. This mirrors
        # fundrawtransaction's own documented requirement that every existing
        # input's previous transaction be known to the wallet;
        # walletfundpsttfee inherits the same constraint from the same
        # underlying coin-selection machinery (doc/tapyrus/pstt.md). Coin
        # selection also dummy-signs every existing input to estimate the
        # final size/fee -- getaddressinfo in this codebase never exposes a
        # 'pubkey' field (DescribeAddressVisitor has no such case), so a
        # plain watch-only import (importaddress/importpubkey) can't give
        # node1 enough to produce even a dummy signature. Importing the
        # private key is the only way in this RPC surface to give a second
        # wallet that ability.
        issue_addr = node0.decoderawtransaction(node0.getrawtransaction(issue_txid))['vout'][issue_vout]['scriptPubKey']['addresses'][0]
        node1.importprivkey(node0.dumpprivkey(issue_addr), "", True)

        recipient_addr = node1.getnewaddress("feeprovider_recv_interactive", colorId)

        # User (primary path): both modifiable flags set, sends unsigned --
        # signing happens only after the provider has funded it, since
        # SIGHASH_ALL (not ANYONECANPAY) is used this time.
        pstt = node0.createpstt(
            [{"txid": issue_txid, "vout": issue_vout}],
            {recipient_addr: 50},
            0, True, True)

        change_addr = node1.getnewaddress()
        funded = node1.walletfundpsttfee(pstt, "interactive", None, change_addr)
        decoded_funded = node1.decodepstt(funded)
        assert_equal(len(decoded_funded['inputs']), 2)
        assert_equal(len(decoded_funded['outputs']), 2)  # recipient + TPC change
        # Interactive mode declares construction finished internally (same
        # effect as finalizepsttconstruction), since it just added both an
        # input and an output together.
        assert_equal(decoded_funded['tx_modifiable']['inputs_modifiable'], False)
        assert_equal(decoded_funded['tx_modifiable']['outputs_modifiable'], False)

        # User verifies (decodepstt already exercised above), then signs its
        # own input with ALL; provider signs its own new input with ALL.
        # Either order works (combinepstt would merge if done independently)
        # -- this test signs sequentially since it's simpler and exercises
        # the same code paths.
        signed1 = node0.walletsignpstt(funded, "ALL", self.options.scheme)['pstt']
        signed2 = node1.walletsignpstt(signed1, "ALL", self.options.scheme)
        assert_equal(signed2['complete'], True)

        final_tx = node0.finalizepstt(signed2['pstt'])['hex']
        node0.sendrawtransaction(final_tx, True)
        node0.generate(1, self.signblockprivkey_wif)
        self.sync_all()

        decoded_final = node0.decoderawtransaction(final_tx)
        recipient_outs = [o for o in decoded_final['vout'] if o['scriptPubKey']['addresses'][0] == recipient_addr]
        assert_equal(len(recipient_outs), 1)

    def _setup_multi_input_fee_utxos(self):
        # Sweep node1's existing TPC balance away first -- coin selection
        # would otherwise happily satisfy the fee from one single leftover
        # UTXO (change from an earlier scenario, or its untouched top-up
        # coinbase) and never need to combine the small UTXOs set up below.
        # node0, not node1, mines every confirmation in this setup, so node1
        # never incidentally re-acquires a large coinbase of its own.
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        leftover = node1.getbalance()
        if leftover > 0:
            node1.sendtoaddress(node0.getnewaddress(), leftover, "", "", True)
        node0.generate(1, self.signblockprivkey_wif)
        self.sync_all()

        # node0 (not node1) pays for these -- node1 must end up with exactly
        # these small UTXOs and no self-generated change, none of which
        # alone can cover the inflated fee set by the caller, so coin
        # selection is forced to combine at least two of them.
        for _ in range(6):
            node0.sendtoaddress(node1.getnewaddress(), 0.002)
        node0.generate(1, self.signblockprivkey_wif)
        self.sync_all()

    def test_interactive_failure(self):
        # Regression test for a bug where walletfundpsttfee's interactive-mode
        # Has-SIGHASH_SINGLE guard only compared vout count to
        # original_output_count == unchanged (i.e. "some inputs added, zero
        # outputs added"). CWallet::CreateTransaction can select more than one
        # input while adding exactly one change output -- e.g. 2 inputs, 1
        # output -- which the old check let straight through even though it
        # breaks addinputoutputpairtopstt's strict 1:1 invariant just as much
        # as the zero-output case. Force that shape here (several TPC UTXOs
        # each too small alone to cover a deliberately inflated fee rate, so
        # coin selection must combine at least two) and confirm it's now
        # rejected instead of silently producing an over-modified PSTT.
        self.log.info("Test walletfundpsttfee, interactive mode rejects a "
                       "SIGHASH_SINGLE PSTT when coin selection needs >1 input")
        node0 = self.nodes[0]  # token-only party
        node1 = self.nodes[1]  # TPC fee provider

        old_fee = node1.getwalletinfo().get('paytxfee', 0)
        # More than a single 0.002 TPC UTXO but comfortably less than all
        # six combined, so at least two (but not necessarily all) must be
        # selected.
        node1.settxfee(0.01)
        try:
            # There is no log line (or any other observable) that reports
            # how many inputs CWallet::CreateTransaction's coin selection
            # picked -- confirm this UTXO/fee setup actually forces >1 input
            # empirically instead, via a control run against a PSTT that
            # doesn't have SIGHASH_SINGLE set (so it isn't rejected) and
            # inspecting the real result.
            self._setup_multi_input_fee_utxos()
            control_issued = create_colored_transaction(2, 25, node0)
            control_colorId = control_issued['color']
            control_issue_txid = control_issued['txid']
            node0.generate(1, self.signblockprivkey_wif)
            self.sync_all()
            control_issue_vout = find_output(node0, control_issue_txid, 25)
            control_issue_addr = node0.decoderawtransaction(node0.getrawtransaction(control_issue_txid))['vout'][control_issue_vout]['scriptPubKey']['addresses'][0]
            node1.importprivkey(node0.dumpprivkey(control_issue_addr), "", True)
            control_recipient_addr = node1.getnewaddress("feeprovider_recv_control", control_colorId)
            control_pstt = node0.createpstt(
                [{"txid": control_issue_txid, "vout": control_issue_vout}],
                {control_recipient_addr: 25},
                0, True, True)  # has_sighash_single left False -- this run must succeed
            control_funded = node1.walletfundpsttfee(control_pstt, "interactive", None, node1.getnewaddress())
            control_decoded = node1.decodepstt(control_funded)
            assert len(control_decoded['inputs']) - 1 >= 2, \
                "control setup only needed one fee input -- test no longer exercises the >1-input case"

            # Now the real regression check, with a fresh batch of small
            # UTXOs (the control run above just spent the first batch).
            self._setup_multi_input_fee_utxos()
            issued = create_colored_transaction(2, 25, node0)
            colorId = issued['color']
            issue_txid = issued['txid']
            node0.generate(1, self.signblockprivkey_wif)
            self.sync_all()
            issue_vout = find_output(node0, issue_txid, 25)

            issue_addr = node0.decoderawtransaction(node0.getrawtransaction(issue_txid))['vout'][issue_vout]['scriptPubKey']['addresses'][0]
            node1.importprivkey(node0.dumpprivkey(issue_addr), "", True)

            recipient_addr = node1.getnewaddress("feeprovider_recv_sighash_single", colorId)

            # has_sighash_single=True: no actual SIGHASH_SINGLE signature is
            # needed to exercise the guard -- createpstt sets
            # PSTT_TXMOD_HAS_SIGHASH_SINGLE directly from this flag, and
            # walletfundpsttfee's check only looks at that bit.
            pstt = node0.createpstt(
                [{"txid": issue_txid, "vout": issue_vout}],
                {recipient_addr: 25},
                0, True, True, True)

            change_addr = node1.getnewaddress()
            assert_raises_rpc_error(-8, "different number of inputs and outputs",
                node1.walletfundpsttfee, pstt, "interactive", None, change_addr)
        finally:
            node1.settxfee(old_fee)


if __name__ == '__main__':
    PSTTFeeProviderTest().main()
