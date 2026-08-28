#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the Partially Signed Tapyrus Transaction (PSTT, TIP-174) RPCs.

Ported from rpc_psbt.py's operational coverage, renamed to the PSTT RPC
surface (see doc/tapyrus/pstt.md). rpc_psbt.py itself carried no
segwit/witness/taproot/bech32-address content to begin with -- Tapyrus never
had segwit -- so nothing needed excluding there; the one BIP-174-specific
concept it did carry (non_witness_utxo, PSBT's "full previous tx" field) maps
directly onto PSTT_IN_UTXO, which fills the same role under a new name.

The old "BIP 174 Test Vectors" section (driven by data/rpc_psbt.json, BIP-174's
own bucket-shaped fixture file) has no PSTT equivalent here at all: TIP-174's
own fixtures (data/tip174_valid.json / data/tip174_invalid.json) are checked
as a C++ unit test instead (src/test/pstt_tests.cpp's pstt_tip174_valid_fixtures
/ pstt_tip174_invalid_fixtures), not re-driven from this functional test --
the two schemas are structurally incompatible with BIP-174's bucket shape
(PSBT v0's global map carries a whole embedded CMutableTransaction under key
0x00, which PSTT reserves as must-reject) so there's no meaningful migration
path, and wire-format/fixture validity is a parsing concern that belongs at
the unit-test layer, not this operational/RPC-behavior layer.

Also adds coverage rpc_psbt.py has no equivalent for: the Constructor role
(addinputtopstt/addoutputtopstt/addinputoutputpairtopstt/
finalizepsttconstruction) -- modifiable-flag enforcement, count-increment
correctness, Has-SIGHASH_SINGLE paired-add enforcement, and a multi-round-trip
scenario simulating several parties incrementally building one PSTT, per
doc/tapyrus/pstt.md's Constructor role description.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, find_output, sync_blocks
from test_framework.blocktools import create_colored_transaction

from decimal import Decimal

import base64
import struct

MAX_BIP125_RBF_SEQUENCE = 0xfffffffd
SEQUENCE_FINAL = 0xffffffff


def make_bare_pstt_with_unknown_global_field():
    """Hand-builds the wire bytes for the simplest possible valid PSTT
    (tx_features=1, zero inputs, zero outputs) plus one PSTT_GLOBAL_PROPRIETARY
    (0xFC) unknown field, in the exact field order
    PartiallySignedTapyrusTransaction::Serialize writes (see doc/tapyrus/pstt.md
    and src/pstt.cpp): magic, tx_features, input_count, output_count, unknown,
    separator. Every key/value is itself a length-prefixed byte string, per
    the wire format doc -- this mirrors rpc_psbt.py's hardcoded
    "unknown_psbt" constant, just PSTT-shaped and generated here instead of
    pasted as an opaque blob, since PSTT has no BIP-174-style published
    "carries unknown fields" test vector to lift one from.
    """
    def lp(b):  # length-prefixed byte string (CompactSize len -- values used here are all < 0xfd)
        return bytes([len(b)]) + b

    magic = bytes([0x70, 0x73, 0x74, 0x74, 0xFF])  # "pstt" + 0xFF
    tx_features = lp(bytes([0x02])) + lp(struct.pack("<i", 1))              # PSTT_GLOBAL_TX_FEATURES = 1
    input_count = lp(bytes([0x04])) + lp(bytes([0x00]))                    # PSTT_GLOBAL_INPUT_COUNT = 0
    output_count = lp(bytes([0x05])) + lp(bytes([0x00]))                   # PSTT_GLOBAL_OUTPUT_COUNT = 0
    unknown = lp(bytes([0xFC, 0x01])) + lp(bytes([0x42]))                  # PSTT_GLOBAL_PROPRIETARY, identifier 0x01, payload 0x42
    separator = bytes([0x00])
    return base64.b64encode(magic + tx_features + input_count + output_count + unknown + separator).decode()


class PSTTTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def run_test(self):
        self.nodes[0].generate(1, self.signblockprivkey_wif)
        #create tokens
        colorId = create_colored_transaction(2, 100, self.nodes[0])['color']
        self.sync_all()
        self.nodes[2].generate(1, self.signblockprivkey_wif)

        # Create and fund a raw tx for sending 10 TPC
        psttx1 = self.nodes[0].walletcreatefundedpstt([], {self.nodes[2].getnewaddress():10})['pstt']

        # Node 1 should not be able to add anything to it but still return the psttx same as before
        psttx = self.nodes[1].walletprocesspstt(psttx1)['pstt']
        assert_equal(psttx1, psttx)

        # Sign the transaction and send
        signed_tx = self.nodes[0].walletprocesspstt(psttx1)['pstt']
        final_tx = self.nodes[0].finalizepstt(signed_tx)['hex']
        self.nodes[0].sendrawtransaction(final_tx, True)

        self.nodes[0].generate(1, self.signblockprivkey_wif)
        self.sync_all()

        # Create and fund a raw tx for sending colored coin
        psttx2 = self.nodes[0].walletcreatefundedpstt([], {self.nodes[2].getnewaddress("tokenpstt", colorId):10, self.nodes[0].getnewaddress("tokenpstt", colorId):90})['pstt']

        psttx = self.nodes[1].walletprocesspstt(psttx2)['pstt']
        assert_equal(psttx2, psttx)
        signed_tx = self.nodes[0].walletprocesspstt(psttx2)['pstt']

        final_tx = self.nodes[0].finalizepstt(signed_tx)['hex']
        self.nodes[0].sendrawtransaction(final_tx, True)

        self.nodes[0].generate(1, self.signblockprivkey_wif)
        self.sync_all()

        # Test decodepstt with colored coin inputs
        self.log.info('Test decodepstt with colored coin inputs')
        cc_pstt = self.nodes[0].walletcreatefundedpstt(
            [],
            {self.nodes[2].getnewaddress("decdpstt", colorId): 5,
             self.nodes[0].getnewaddress("decdpstt", colorId): 85}
        )['pstt']
        # walletprocesspstt attaches PSTT_IN_UTXO for each input
        cc_processed = self.nodes[0].walletprocesspstt(cc_pstt)
        assert_equal(cc_processed['complete'], True)
        cc_decoded = self.nodes[0].decodepstt(cc_processed['pstt'])
        for inp in cc_decoded['inputs']:
            assert 'utxo' in inp
        # doc/tapyrus/pstt.md's Colored Coin balance verification section:
        # every input/output is annotated with the existing token/amount
        # display convention.
        for out in cc_decoded['outputs']:
            assert 'token' in out
            assert 'amount' in out

        # Test walletprocesspstt rejects out-of-bounds prevout.n
        self.log.info('Test walletprocesspstt rejects out-of-bounds prevout.n')
        utxo = self.nodes[0].listunspent()[0]
        raw_utxo_tx = self.nodes[0].getrawtransaction(utxo['txid'], True)
        oob_vout = len(raw_utxo_tx['vout']) + 5
        bad_pstt = self.nodes[0].createpstt(
            [{"txid": utxo['txid'], "vout": oob_vout}],
            {self.nodes[0].getnewaddress(): utxo['amount'] - Decimal('0.01')}
        )
        assert_raises_rpc_error(-22, "Input prevout index out of range",
                                self.nodes[0].walletprocesspstt, bad_pstt)

        # Create p2sh and p2pkh addresses
        pubkey0 = self.nodes[0].getaddressinfo(self.nodes[0].getnewaddress())['pubkey']
        pubkey1 = self.nodes[1].getaddressinfo(self.nodes[1].getnewaddress())['pubkey']
        pubkey2 = self.nodes[2].getaddressinfo(self.nodes[2].getnewaddress())['pubkey']
        p2sh = self.nodes[1].addmultisigaddress(2, [pubkey0, pubkey1, pubkey2], "")['address']
        p2pkh = self.nodes[1].getnewaddress("")
        cp2pkh = self.nodes[1].getnewaddress("", colorId)

        # fund those addresses
        rawtx = self.nodes[0].createrawtransaction([], {p2sh:10, p2pkh:10})
        rawtx = self.nodes[0].fundrawtransaction(rawtx, {"changePosition":2})
        rawtx = self.nodes[0].createrawtransaction([], {cp2pkh :10})
        rawtx = self.nodes[0].fundrawtransaction(rawtx)
        rawtx = self.nodes[0].createrawtransaction([], {p2sh:30, p2pkh:30, cp2pkh : 10})
        rawtx = self.nodes[0].fundrawtransaction(rawtx)
        signed_tx = self.nodes[0].signrawtransactionwithwallet(rawtx['hex'], [], "ALL", self.options.scheme)['hex']
        txid = self.nodes[0].sendrawtransaction(signed_tx, True)
        self.nodes[0].generate(6, self.signblockprivkey_wif)
        self.sync_all()

        # Find the output pos
        p2sh_pos = -1
        p2pkh_pos = -1
        decoded = self.nodes[0].decoderawtransaction(signed_tx)
        for out in decoded['vout']:
            if out['scriptPubKey']['addresses'][0] == p2sh:
                p2sh_pos = out['n']
            elif out['scriptPubKey']['addresses'][0] == p2pkh:
                p2pkh_pos = out['n']

        # spend single key from node 1
        rawtx = self.nodes[1].walletcreatefundedpstt([{"txid":txid,"vout":p2pkh_pos}], {self.nodes[1].getnewaddress():29.99})['pstt']
        walletprocesspstt_out = self.nodes[1].walletprocesspstt(rawtx)
        assert_equal(walletprocesspstt_out['complete'], True)
        self.nodes[1].sendrawtransaction(self.nodes[1].finalizepstt(walletprocesspstt_out['pstt'])['hex'], True)

        # partially sign multisig things with node 1
        psttx = self.nodes[1].walletcreatefundedpstt([{"txid":txid,"vout":p2sh_pos}], {self.nodes[1].getnewaddress():29.99})['pstt']
        walletprocesspstt_out = self.nodes[1].walletprocesspstt(psttx)
        psttx = walletprocesspstt_out['pstt']
        assert_equal(walletprocesspstt_out['complete'], False)

        # partially sign with node 2. This should be complete and sendable
        walletprocesspstt_out = self.nodes[2].walletprocesspstt(psttx)
        assert_equal(walletprocesspstt_out['complete'], True)
        self.nodes[2].sendrawtransaction(self.nodes[2].finalizepstt(walletprocesspstt_out['pstt'])['hex'], True)

        # check that walletprocesspstt fails to decode a non-pstt
        rawtx = self.nodes[1].createrawtransaction([{"txid":txid,"vout":p2pkh_pos}], {self.nodes[1].getnewaddress():9.99})
        assert_raises_rpc_error(-22, "TX decode failed", self.nodes[1].walletprocesspstt, rawtx)

        # Convert a non-pstt to pstt and make sure we can decode it
        rawtx = self.nodes[0].createrawtransaction([], {self.nodes[1].getnewaddress():10})
        rawtx = self.nodes[0].fundrawtransaction(rawtx)
        new_pstt = self.nodes[0].converttopstt(rawtx['hex'])
        self.nodes[0].decodepstt(new_pstt)

        # Make sure that a pstt with signatures cannot be converted
        signedtx = self.nodes[0].signrawtransactionwithwallet(rawtx['hex'], [], "ALL", self.options.scheme)
        assert_raises_rpc_error(-22, "Inputs must not have scriptSigs", self.nodes[0].converttopstt, signedtx['hex'])

        # Explicitly allow converting non-empty txs
        new_pstt = self.nodes[0].converttopstt(rawtx['hex'])
        self.nodes[0].decodepstt(new_pstt)

        # Create outputs to nodes 1 and 2
        node1_addr = self.nodes[1].getnewaddress()
        node2_addr = self.nodes[2].getnewaddress()
        txid1 = self.nodes[0].sendtoaddress(node1_addr, 13)
        txid2 =self.nodes[0].sendtoaddress(node2_addr, 13)
        self.nodes[0].generate(6, self.signblockprivkey_wif)
        sync_blocks(self.nodes)
        #mempool is not synched.
        vout1 = find_output(self.nodes[1], txid1, 13)
        vout2 = find_output(self.nodes[2], txid2, 13)

        # Create a pstt spending outputs from nodes 1 and 2
        pstt_orig = self.nodes[0].createpstt([{"txid":txid1, "vout":vout1}, {"txid":txid2, "vout":vout2}], {self.nodes[0].getnewaddress():25.999})

        # Update pstts, should only have data for one input and not the other.
        # Unlike decodepsbt (whose per-input entries are empty {} until
        # something is explicitly added, since PSBT's embedded global tx
        # already carries txid/vout), PSTT's decodepstt
        # per-input entries always carry txid/vout -- PSTT
        # has no separate global tx to fall back on for those, so the dict
        # is never actually empty. Check for walletprocesspstt's
        # Updater-attached utxo specifically instead of dict truthiness.
        pstt1 = self.nodes[1].walletprocesspstt(pstt_orig)['pstt']
        pstt1_decoded = self.nodes[0].decodepstt(pstt1)
        assert 'utxo' in pstt1_decoded['inputs'][0] and 'utxo' not in pstt1_decoded['inputs'][1]
        pstt2 = self.nodes[2].walletprocesspstt(pstt_orig)['pstt']
        pstt2_decoded = self.nodes[0].decodepstt(pstt2)
        assert 'utxo' not in pstt2_decoded['inputs'][0] and 'utxo' in pstt2_decoded['inputs'][1]

        # Combine, finalize, and send the pstts
        combined = self.nodes[0].combinepstt([pstt1, pstt2])
        finalized = self.nodes[0].finalizepstt(combined)['hex']
        self.nodes[0].sendrawtransaction(finalized, True)
        self.nodes[0].generate(6, self.signblockprivkey_wif)
        sync_blocks(self.nodes)
        #mempool is not synched.

        # Test additional args in walletcreatefundedpstt
        # Make sure both pre-included and funded inputs
        # have the correct sequence numbers based on
        # replaceable arg
        block_height = self.nodes[0].getblockcount()
        # Same colored-change hazard as above: node0 already holds colored
        # UTXOs by this point, and this input is paired with a plain
        # (uncolored) destination below.
        unspent = next(u for u in self.nodes[0].listunspent() if u.get('token') == 'TPC')
        psttx_info = self.nodes[0].walletcreatefundedpstt([{"txid":unspent["txid"], "vout":unspent["vout"]}], [{self.nodes[2].getnewaddress():unspent["amount"]+1}], block_height+2, {"replaceable":True}, False)
        decoded_pstt = self.nodes[0].decodepstt(psttx_info["pstt"])
        for pstt_in in decoded_pstt["inputs"]:
           assert_equal(pstt_in["sequence"], MAX_BIP125_RBF_SEQUENCE)
           assert "bip32_derivs" not in pstt_in
        assert_equal(decoded_pstt["locktime"], block_height+2)

        # Same construction with only locktime set
        psttx_info = self.nodes[0].walletcreatefundedpstt([{"txid":unspent["txid"], "vout":unspent["vout"]}], [{self.nodes[2].getnewaddress():unspent["amount"]+1}], block_height, {}, True)
        decoded_pstt = self.nodes[0].decodepstt(psttx_info["pstt"])
        for pstt_in in decoded_pstt["inputs"]:
            assert pstt_in["sequence"] > MAX_BIP125_RBF_SEQUENCE
            assert "bip32_derivs" in pstt_in
        assert_equal(decoded_pstt["locktime"], block_height)

        # Same construction without optional arguments. With no locktime and
        # no replaceable flag, PSTT_IN_SEQUENCE stays at its implicit default
        # (0xFFFFFFFF) and PsttInputToUniv only emits "sequence" when the
        # field is actually present on the wire (decodepstt shows what's
        # stored, not a materialized/implied value -- unlike decodepsbt,
        # which always has a concrete per-input sequence via its embedded
        # global tx) -- so this checks the effective value via .get(), not a
        # bare index, since "sequence" may legitimately be absent here.
        psttx_info = self.nodes[0].walletcreatefundedpstt([{"txid":unspent["txid"], "vout":unspent["vout"]}], [{self.nodes[2].getnewaddress():unspent["amount"]+1}])
        decoded_pstt = self.nodes[0].decodepstt(psttx_info["pstt"])
        for pstt_in in decoded_pstt["inputs"]:
            assert pstt_in.get("sequence", SEQUENCE_FINAL) > MAX_BIP125_RBF_SEQUENCE
        assert_equal(decoded_pstt["locktime"], 0)

        # Test the ECDSA/Schnorr sigscheme parameter end to end
        self.log.info('Test walletsignpstt/walletprocesspstt with sigscheme=SCHNORR')
        schnorr_pstt = self.nodes[0].walletcreatefundedpstt([], {self.nodes[2].getnewaddress():5})['pstt']
        schnorr_out = self.nodes[0].walletprocesspstt(schnorr_pstt, True, "ALL", False, "SCHNORR")
        assert_equal(schnorr_out['complete'], True)
        self.nodes[0].sendrawtransaction(self.nodes[0].finalizepstt(schnorr_out['pstt'])['hex'], True)
        self.nodes[0].generate(1, self.signblockprivkey_wif)
        self.sync_all()

        # Check that unknown global fields are just passed through --
        # PSTT-shaped analog of rpc_psbt.py's hardcoded "unknown_psbt" case,
        # hand-built here since PSTT has no equivalent published test vector
        # (see make_bare_pstt_with_unknown_global_field's docstring).
        self.log.info('Test unknown fields are passed through unchanged')
        unknown_pstt = make_bare_pstt_with_unknown_global_field()
        self.nodes[0].decodepstt(unknown_pstt)  # must not throw
        unknown_out = self.nodes[0].walletprocesspstt(unknown_pstt)['pstt']
        assert_equal(unknown_pstt, unknown_out)

        # Constructor role: addinputtopstt/addoutputtopstt/
        # addinputoutputpairtopstt/finalizepsttconstruction. rpc_psbt.py has
        # no equivalent section -- PSBT's Constructor is folded silently into
        # createpsbt/walletcreatefundedpsbt with no standalone incremental
        # RPCs, so this is new coverage specific to TIP-174's constructable
        # (BIP-370-style) data model (doc/tapyrus/pstt.md's Constructor role).
        self.log.info('Test Constructor role: addinputtopstt/addoutputtopstt/addinputoutputpairtopstt/finalizepsttconstruction')

        # createpstt with both modifiable flags set.
        bare = self.nodes[0].createpstt([], [], 0, True, True)
        decoded_bare = self.nodes[0].decodepstt(bare)
        assert_equal(decoded_bare['tx_modifiable']['inputs_modifiable'], True)
        assert_equal(decoded_bare['tx_modifiable']['outputs_modifiable'], True)
        assert_equal(len(decoded_bare['inputs']), 0)
        assert_equal(len(decoded_bare['outputs']), 0)

        # Neither Constructor RPC works once construction is not modifiable.
        self.nodes[0].generate(5, self.signblockprivkey_wif)
        self.sync_all()
        # node0 also holds colored-coin change from earlier in this test
        # (create_colored_transaction plus the walletcreatefundedpstt/
        # decodepstt colored-output tests above) -- filter to TPC only, or
        # an unfiltered [0:4] could silently pick a colored UTXO here.
        unspent_list = [u for u in self.nodes[0].listunspent() if u.get('token') == 'TPC']
        assert len(unspent_list) >= 4, "test needs at least 4 spendable TPC UTXOs on node0 at this point"
        unspent_a, unspent_b, unspent_c, unspent_d = unspent_list[0:4]

        frozen = self.nodes[0].createpstt([], [], 0, False, False)
        assert_raises_rpc_error(-8, "PSTT inputs are not modifiable",
                                 self.nodes[0].addinputtopstt, frozen, unspent_a['txid'], unspent_a['vout'])
        assert_raises_rpc_error(-8, "PSTT outputs are not modifiable",
                                 self.nodes[0].addoutputtopstt, frozen, {self.nodes[1].getnewaddress(): 1})

        # Count-increment correctness, alternating "parties" (nodes) --
        # a multi-round-trip incremental construction, the shape the Fee
        # Provider workflow (doc/tapyrus/pstt.md) needs.
        pstt_construct = self.nodes[0].addinputtopstt(bare, unspent_a['txid'], unspent_a['vout'])
        decoded = self.nodes[0].decodepstt(pstt_construct)
        assert_equal(len(decoded['inputs']), 1)
        assert_equal(len(decoded['outputs']), 0)
        assert_equal(decoded['inputs'][0]['txid'], unspent_a['txid'])

        pstt_construct = self.nodes[1].addoutputtopstt(pstt_construct, {self.nodes[1].getnewaddress(): 1})
        decoded = self.nodes[0].decodepstt(pstt_construct)
        assert_equal(len(decoded['inputs']), 1)
        assert_equal(len(decoded['outputs']), 1)

        pstt_construct = self.nodes[2].addinputtopstt(pstt_construct, unspent_b['txid'], unspent_b['vout'])
        decoded = self.nodes[0].decodepstt(pstt_construct)
        assert_equal(len(decoded['inputs']), 2)
        assert_equal(len(decoded['outputs']), 1)

        # Has-SIGHASH_SINGLE: addinputtopstt/addoutputtopstt are individually
        # refused; addinputoutputpairtopstt is required instead.
        single_pstt = self.nodes[0].createpstt([], [], 0, True, True, True)
        decoded_single = self.nodes[0].decodepstt(single_pstt)
        assert_equal(decoded_single['tx_modifiable']['has_sighash_single'], True)
        assert_raises_rpc_error(-8, "addinputoutputpairtopstt",
                                 self.nodes[0].addinputtopstt, single_pstt, unspent_c['txid'], unspent_c['vout'])
        assert_raises_rpc_error(-8, "addinputoutputpairtopstt",
                                 self.nodes[0].addoutputtopstt, single_pstt, {self.nodes[1].getnewaddress(): 1})
        paired = self.nodes[0].addinputoutputpairtopstt(single_pstt, unspent_c['txid'], unspent_c['vout'], {self.nodes[1].getnewaddress(): 1})
        decoded_paired = self.nodes[0].decodepstt(paired)
        assert_equal(len(decoded_paired['inputs']), 1)
        assert_equal(len(decoded_paired['outputs']), 1)

        # finalizepsttconstruction declares construction finished; further
        # Constructor calls are then refused.
        finished = self.nodes[0].finalizepsttconstruction(pstt_construct)
        decoded_finished = self.nodes[0].decodepstt(finished)
        assert_equal(decoded_finished['tx_modifiable']['inputs_modifiable'], False)
        assert_equal(decoded_finished['tx_modifiable']['outputs_modifiable'], False)
        assert_raises_rpc_error(-8, "PSTT inputs are not modifiable",
                                 self.nodes[0].addinputtopstt, finished, unspent_d['txid'], unspent_d['vout'])

        # Sign and broadcast the multi-round-trip-constructed PSTT to prove
        # it's a real, valid transaction end to end, not just structurally
        # well-formed.
        processed = self.nodes[0].walletprocesspstt(finished)
        assert_equal(processed['complete'], True)
        self.nodes[0].sendrawtransaction(self.nodes[0].finalizepstt(processed['pstt'])['hex'], True)
        self.nodes[0].generate(1, self.signblockprivkey_wif)
        self.sync_all()

        # finalizepsttconstruction's clear_inputs_modifiable/
        # clear_outputs_modifiable can also be applied selectively.
        partial = self.nodes[0].createpstt([], [], 0, True, True)
        partial = self.nodes[0].finalizepsttconstruction(partial, True, False)
        decoded_partial = self.nodes[0].decodepstt(partial)
        assert_equal(decoded_partial['tx_modifiable']['inputs_modifiable'], False)
        assert_equal(decoded_partial['tx_modifiable']['outputs_modifiable'], True)


if __name__ == '__main__':
    PSTTTest().main()
