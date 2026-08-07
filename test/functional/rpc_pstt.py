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
own bucket-shaped fixture file) is replaced outright by TIP-174's own fixtures
(data/tip174_valid.json / data/tip174_invalid.json -- see data/tip174_SOURCE.md
for provenance) -- the two schemas are structurally incompatible (PSBT v0's
global map carries a whole embedded CMutableTransaction under key 0x00, which
PSTT reserves as must-reject), so there is no meaningful migration path.

Note: this file assumes the createpstt/converttopstt/addinputtopstt/
addoutputtopstt/finalizepsttconstruction/combinepstt/finalizepstt/extractpstt/
decodepstt/walletcreatefundedpstt/walletupdatepstt/walletprocesspstt/
walletsignpstt RPCs described in doc/tapyrus/pstt.md exist. It is not
runnable until those land (non-wallet RPCs, Constructor/Combiner/Finalizer/
Extractor primitives, and the wallet glue). decodepstt's exact JSON field
names are a best-effort match to doc/tapyrus/pstt.md and may need small
adjustment once that RPC actually ships.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, find_output, sync_blocks, TAPYRUS_MODES
from test_framework.blocktools import create_colored_transaction

from decimal import Decimal

import json
import os

MAX_BIP125_RBF_SEQUENCE = 0xfffffffd

DATA_DIR = os.path.join(os.path.dirname(os.path.realpath(__file__)), 'data')

REQUIRED_VALID_ENTRY_KEYS = {'id', 'description', 'intermediates', 'stages', 'extracted_tx', 'final_txid'}
REQUIRED_STAGE_KEYS = {'name', 'pstt'}
REQUIRED_INVALID_ENTRY_KEYS = {'id', 'description', 'pstt', 'expected'}
REQUIRED_EXPECTED_KEYS = {'valid', 'stage', 'reason'}
VALID_STAGE_NAMES = {'parse', 'rule'}


def load_valid_fixtures():
    """Load tip174_valid.json and validate its top-level schema defensively.

    Fails loudly (raises) rather than silently accepting a malformed/misread schema, per
    the plan's §9b instruction: a subtle schema misread should surface immediately, not
    silently pass zero test cases.
    """
    path = os.path.join(DATA_DIR, 'tip174_valid.json')
    with open(path, encoding='utf-8') as f:
        entries = json.load(f)
    if not isinstance(entries, list):
        raise AssertionError("tip174_valid.json: expected a top-level array, got %s" % type(entries).__name__)
    for entry in entries:
        missing = REQUIRED_VALID_ENTRY_KEYS - entry.keys()
        if missing:
            raise AssertionError("tip174_valid.json entry %r missing required key(s): %s" % (entry.get('id', '?'), sorted(missing)))
        if not isinstance(entry['stages'], list) or not entry['stages']:
            raise AssertionError("tip174_valid.json entry %r: 'stages' must be a non-empty array" % entry['id'])
        for stage in entry['stages']:
            missing = REQUIRED_STAGE_KEYS - stage.keys()
            if missing:
                raise AssertionError(
                    "tip174_valid.json entry %r stage %r missing required key(s): %s"
                    % (entry['id'], stage.get('name', '?'), sorted(missing)))
    return entries


def load_invalid_fixtures():
    """Load tip174_invalid.json and validate its top-level schema defensively."""
    path = os.path.join(DATA_DIR, 'tip174_invalid.json')
    with open(path, encoding='utf-8') as f:
        entries = json.load(f)
    if not isinstance(entries, list):
        raise AssertionError("tip174_invalid.json: expected a top-level array, got %s" % type(entries).__name__)
    for entry in entries:
        missing = REQUIRED_INVALID_ENTRY_KEYS - entry.keys()
        if missing:
            raise AssertionError("tip174_invalid.json entry %r missing required key(s): %s" % (entry.get('id', '?'), sorted(missing)))
        expected = entry['expected']
        missing = REQUIRED_EXPECTED_KEYS - expected.keys()
        if missing:
            raise AssertionError("tip174_invalid.json entry %r 'expected' missing required key(s): %s" % (entry['id'], sorted(missing)))
        if expected['valid'] is not False:
            raise AssertionError("tip174_invalid.json entry %r: expected.valid must be false" % entry['id'])
        if expected['stage'] not in VALID_STAGE_NAMES:
            raise AssertionError(
                "tip174_invalid.json entry %r: expected.stage must be one of %s, got %r"
                % (entry['id'], sorted(VALID_STAGE_NAMES), expected['stage']))
    return entries


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
        # decision #15 (doc/tapyrus/pstt.md): every input/output is annotated
        # with the existing token/amount display convention.
        for out in cc_decoded['outputs']:
            assert 'token' in out
            assert 'amount' in out

        # Test walletprocesspstt rejects out-of-bounds prevout.n
        self.log.info('Test walletprocesspstt rejects out-of-bounds prevout.n')
        utxo = self.nodes[0].listunspent()[0]
        raw_utxo_tx = self.nodes[0].getrawtransaction(utxo['txid'], True)
        oob_vout = len(raw_utxo_tx['vout']) + 5
        bad_pstt = self.nodes[0].createpstt(
            [{"previous_txid": utxo['txid'], "output_index": oob_vout}],
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
        rawtx = self.nodes[1].walletcreatefundedpstt([{"previous_txid":txid,"output_index":p2pkh_pos}], {self.nodes[1].getnewaddress():29.99})['pstt']
        walletprocesspstt_out = self.nodes[1].walletprocesspstt(rawtx)
        assert_equal(walletprocesspstt_out['complete'], True)
        self.nodes[1].sendrawtransaction(self.nodes[1].finalizepstt(walletprocesspstt_out['pstt'])['hex'], True)

        # partially sign multisig things with node 1
        psttx = self.nodes[1].walletcreatefundedpstt([{"previous_txid":txid,"output_index":p2sh_pos}], {self.nodes[1].getnewaddress():29.99})['pstt']
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
        pstt_orig = self.nodes[0].createpstt([{"previous_txid":txid1, "output_index":vout1}, {"previous_txid":txid2, "output_index":vout2}], {self.nodes[0].getnewaddress():25.999})

        # Update pstts, should only have data for one input and not the other
        pstt1 = self.nodes[1].walletprocesspstt(pstt_orig)['pstt']
        pstt1_decoded = self.nodes[0].decodepstt(pstt1)
        assert pstt1_decoded['inputs'][0] and not pstt1_decoded['inputs'][1]
        pstt2 = self.nodes[2].walletprocesspstt(pstt_orig)['pstt']
        pstt2_decoded = self.nodes[0].decodepstt(pstt2)
        assert not pstt2_decoded['inputs'][0] and pstt2_decoded['inputs'][1]

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
        unspent = self.nodes[0].listunspent()[0]
        psttx_info = self.nodes[0].walletcreatefundedpstt([{"previous_txid":unspent["txid"], "output_index":unspent["vout"]}], [{self.nodes[2].getnewaddress():unspent["amount"]+1}], block_height+2, {"replaceable":True}, False)
        decoded_pstt = self.nodes[0].decodepstt(psttx_info["pstt"])
        for pstt_in in decoded_pstt["inputs"]:
           assert_equal(pstt_in["sequence"], MAX_BIP125_RBF_SEQUENCE)
           assert "bip32_derivs" not in pstt_in
        assert_equal(decoded_pstt["locktime"], block_height+2)

        # Same construction with only locktime set
        psttx_info = self.nodes[0].walletcreatefundedpstt([{"previous_txid":unspent["txid"], "output_index":unspent["vout"]}], [{self.nodes[2].getnewaddress():unspent["amount"]+1}], block_height, {}, True)
        decoded_pstt = self.nodes[0].decodepstt(psttx_info["pstt"])
        for pstt_in in decoded_pstt["inputs"]:
            assert pstt_in["sequence"] > MAX_BIP125_RBF_SEQUENCE
            assert "bip32_derivs" in pstt_in
        assert_equal(decoded_pstt["locktime"], block_height)

        # Same construction without optional arguments
        psttx_info = self.nodes[0].walletcreatefundedpstt([{"previous_txid":unspent["txid"], "output_index":unspent["vout"]}], [{self.nodes[2].getnewaddress():unspent["amount"]+1}])
        decoded_pstt = self.nodes[0].decodepstt(psttx_info["pstt"])
        for pstt_in in decoded_pstt["inputs"]:
            assert pstt_in["sequence"] > MAX_BIP125_RBF_SEQUENCE
        assert_equal(decoded_pstt["locktime"], 0)

        # Test the ECDSA/Schnorr sigscheme parameter end to end
        self.log.info('Test walletsignpstt/walletprocesspstt with sigscheme=SCHNORR')
        schnorr_pstt = self.nodes[0].walletcreatefundedpstt([], {self.nodes[2].getnewaddress():5})['pstt']
        schnorr_out = self.nodes[0].walletprocesspstt(schnorr_pstt, True, "ALL", False, "SCHNORR")
        assert_equal(schnorr_out['complete'], True)
        self.nodes[0].sendrawtransaction(self.nodes[0].finalizepstt(schnorr_out['pstt'])['hex'], True)
        self.nodes[0].generate(1, self.signblockprivkey_wif)
        self.sync_all()

        # TIP-174 test vectors
        self.test_tip174_fixtures()

    def test_tip174_fixtures(self):
        # TIP-174's fixtures are dev-network-only (WIF 0xef, P2PKH 0x6f, P2SH
        # 0xc4, CP2PKH 0x70, CP2SH 0xc5, BIP32 tpub/tprv -- see
        # data/tip174_SOURCE.md). Under the strict Params()-only xpub prefix
        # policy (doc/tapyrus/pstt.md), a future accidental PROD-mode run of
        # this test would otherwise fail confusingly, far from the actual
        # cause, on the one xpub-bearing entry -- assert explicitly instead.
        assert self.mode == TAPYRUS_MODES.DEV, (
            "rpc_pstt.py's TIP-174 fixtures are dev-network-only; refusing to run under %s" % self.mode)

        self.log.info("Loading TIP-174 fixtures (see data/tip174_SOURCE.md for provenance)")
        valid_entries = load_valid_fixtures()
        invalid_entries = load_invalid_fixtures()
        self.log.info("Loaded %d valid workflow(s), %d invalid vector(s)", len(valid_entries), len(invalid_entries))

        node = self.nodes[0]

        # valid.json: walk every stage's PSTT, asserting it decodes and that
        # identification_txid is constant across every stage in the entry (per
        # spec, the identifier must not change as the PSTT is filled in).
        # Re-deriving each intermediate stage from the previous one via our
        # own RPCs isn't attempted here -- the fixtures were signed with a
        # generator-internal deterministic master key (see data/tip174_SOURCE.md)
        # that isn't loaded into any node's wallet, so "sign this ourselves and
        # compare bytes" isn't available for the signed/updated stages. What is
        # checked on every stage: it decodes without error, and (via decodepstt)
        # its identification_txid matches every other stage in the same entry.
        # What is checked with full strength, on the entry's LAST stage only:
        # extractpstt reproduces extracted_tx exactly, and sendrawtransaction
        # accepts it with the expected final_txid -- i.e. a real,
        # network-valid transaction comes out the other end of the pipeline.
        for entry in valid_entries:
            identification_txids = set()
            for stage in entry['stages']:
                decoded = node.decodepstt(stage['pstt'])
                if 'identification_txid' in stage:
                    identification_txids.add(stage['identification_txid'])
                    assert_equal(decoded['identification_txid'], stage['identification_txid'])
            assert_equal(len(identification_txids), 1,
                         "entry %r: identification_txid changed across stages" % entry['id'])

            last_pstt = entry['stages'][-1]['pstt']
            extracted = node.extractpstt(last_pstt)['hex']
            assert_equal(extracted, entry['extracted_tx'])
            txid = node.sendrawtransaction(extracted, True)
            assert_equal(txid, entry['final_txid'])
            node.generate(1, self.signblockprivkey_wif)

        # invalid.json: "parse" entries must be rejected by decodepstt itself;
        # "rule" entries must decode fine (structurally well-formed) but fail
        # a role-primitive check -- finalizepstt's completeness pass runs
        # every input through the same checks SignPSTTInput does (UTXO
        # presence/match, redeem-script hash, SIGHASH_SINGLE bounds, locktime
        # validity) via a dummy signing provider, so it's a generically
        # applicable role check regardless of which specific rule an entry
        # is exercising.
        for entry in invalid_entries:
            expected_stage = entry['expected']['stage']
            if expected_stage == 'parse':
                assert_raises_rpc_error(-22, "TX decode failed", node.decodepstt, entry['pstt'])
            else:
                node.decodepstt(entry['pstt']) # must not throw
                result = node.finalizepstt(entry['pstt'], False)
                assert_equal(result['complete'], False)


if __name__ == '__main__':
    PSTTTest().main()
