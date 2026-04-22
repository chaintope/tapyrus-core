#!/usr/bin/env python3
# Copyright (c) 2024 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Vulnerability tests for colored coin consensus rules.

Each attack scenario is verified across all submission paths:
  - RPC sendrawtransaction (mempool acceptance)
  - RPC submitblock (block validation)
  - P2P msg_block (direct block announcement)
  - P2P msg_cmpctblock (compact block announcement)
  - -loadblock file (block file import on restart, tests TestBlockValidity path)

Attack scenarios (run for each applicable token type):
  1. Duplicate outputs in a single issuance tx (same colorId, two outputs)
     — NFT only (bad-txns-nft-output-count)
     NON_REISSUABLE allows multiple outputs (split supply); REISSUABLE allows re-issuance by design.
  2. Re-issuance of an already-issued colorId
     — NON_REISSUABLE, NFT  (REISSUABLE allows re-issuance by design)
  3. NFT transfer with two outputs of same colorId (bad-txns-nft-output-count)
     — NFT only
  4. NFT with nValue != 1 (value inflation)
     — NFT only
  5. Colored output in coinbase (consensus bypass)
     — REISSUABLE, NON_REISSUABLE, NFT
  6. Colored script in genesis coinbase: colorId derived from genesis coinbase outpoint
     put into a block coinbase — rejected by CheckBlock ("bad-cb-issuetoken")
     — REISSUABLE, NON_REISSUABLE, NFT
  7. Colored issue using genesis coinbase outpoint as input: genesis coinbase is
     unspendable (not in UTXO set), so any TX referencing it is rejected
     with "Missing inputs" — NON_REISSUABLE, NFT

Valid multi-op scenarios (all in one transaction):
  6.  One issue + one transfer + one burn
  7.  Two token types issued from the same TPC input
  8.  Two issues + one burn
  9.  Two transfers + one burn
  10. One transfer + two burns
  11. Three burns
"""

import os
import struct
from decimal import Decimal

from test_framework.blocktools import (
    create_block,
    create_coinbase,
    findTPC,
)
from test_framework.messages import (
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    FromHex,
    HeaderAndShortIDs,
    msg_block,
    msg_cmpctblock,
    ToHex,
    sha256,
)
from test_framework.mininode import P2PInterface
from test_framework.script import CScript, OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG, OP_COLOR, hash160
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_raises_rpc_error,
    bytes_to_hex_str,
    hex_str_to_bytes,
    MagicBytes,
    wait_until,
)

REISSUABLE     = 0xc1
NON_REISSUABLE = 0xc2
NFT            = 0xc3

TOKEN_TYPE_NUM  = {REISSUABLE: 1, NON_REISSUABLE: 2, NFT: 3}
TOKEN_TYPE_NAME = {REISSUABLE: 'REISSUABLE', NON_REISSUABLE: 'NON_REISSUABLE', NFT: 'NFT'}


def default_amount(token_type):
    """Default issuance amount: NFT must be 1; others use 100."""
    return 1 if token_type == NFT else 100


def color_id(token_type: int, prevout_bytes: bytes) -> bytes:
    """Compute the 33-byte colorId for any token type from a prevout."""
    return bytes([token_type]) + sha256(prevout_bytes)


def serialize_outpoint(txid_hex: str, vout: int) -> bytes:
    """Serialize a COutPoint (little-endian txid + 4-byte vout) as used in colorId hashing."""
    return hex_str_to_bytes(txid_hex)[::-1] + struct.pack('<I', vout)


def p2pkh_script(pubkey_hash: bytes) -> CScript:
    return CScript([OP_DUP, OP_HASH160, pubkey_hash, OP_EQUALVERIFY, OP_CHECKSIG])


def colored_p2pkh_script(colorid_bytes: bytes, pubkey_hash: bytes) -> CScript:
    return CScript([colorid_bytes, OP_COLOR, OP_DUP, OP_HASH160, pubkey_hash, OP_EQUALVERIFY, OP_CHECKSIG])


def sign_and_get_hex(node, raw_tx):
    signed = node.signrawtransactionwithwallet(raw_tx)
    assert signed['complete'], "tx signing failed: %s" % signed.get('errors')
    return signed['hex']


def submit_via_p2p(peer, block):
    peer.send_message(msg_block(block))


def submit_via_compact_block(peer, block):
    cmpct = HeaderAndShortIDs()
    cmpct.initialize_from_block(block, prefill_list=[0])
    peer.send_message(msg_cmpctblock(cmpct.to_p2p()))


def write_block_file(path, magic, blocks):
    with open(path, 'wb') as f:
        for block in blocks:
            raw = block.serialize()
            f.write(magic)
            f.write(struct.pack('<I', len(raw)))
            f.write(raw)


class ColoredCoinVulnTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def setup_network(self):
        super().setup_network()
        self.peer = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))

    # ------------------------------------------------------------------ helpers

    def get_spendable_utxo(self):
        utxos = self.nodes[0].listunspent()
        tpc_utxos = [u for u in utxos if u['token'] == 'TPC']
        assert tpc_utxos, "no TPC UTXO available"
        return max(tpc_utxos, key=lambda u: u['amount'])

    def get_pubkey_hash(self):
        node = self.nodes[0]
        return hash160(hex_str_to_bytes(node.getaddressinfo(node.getnewaddress())['pubkey']))

    def issue_token(self, token_type, amount=None):
        """Issue a token via RPC and return result. Does NOT mine.

        For REISSUABLE tokens issuetoken takes the coin's scriptPubKey (hex)
        as the 3rd argument; the colorId is sha256(scriptPubKey).
        For NON_REISSUABLE and NFT issuetoken takes txid + vout; the colorId
        is sha256(outpoint).
        """
        node = self.nodes[0]
        utxo = self.get_spendable_utxo()
        if amount is None:
            amount = default_amount(token_type)
        if token_type == REISSUABLE:
            result = node.issuetoken(TOKEN_TYPE_NUM[token_type], amount, utxo['scriptPubKey'])
        else:
            result = node.issuetoken(TOKEN_TYPE_NUM[token_type], amount, utxo['txid'], utxo['vout'])
        return result

    def find_colored_utxo(self, color_hex):
        utxo = next((u for u in self.nodes[0].listunspent() if u['token'] == color_hex), None)
        assert utxo is not None, "colored UTXO not found for color %s" % color_hex
        return utxo

    def make_colored_issuance_tx(self, amount, utxo, colorid_bytes, pubkey_hash, extra_colored_outputs=None):
        """
        Build a raw colored-coin issuance tx (NOT signed yet).
        extra_colored_outputs: list of (colorid_bytes, amount) for additional colored vouts.
        """
        fee = Decimal('0.001')
        change = utxo['amount'] - fee

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(utxo['txid'], 16), utxo['vout']), b'', 0xffffffff))

        colored_out = CTxOut()
        colored_out.nValue = amount
        colored_out.scriptPubKey = colored_p2pkh_script(colorid_bytes, pubkey_hash)
        tx.vout.append(colored_out)

        if extra_colored_outputs:
            for extra_cid, extra_amt in extra_colored_outputs:
                extra_out = CTxOut()
                extra_out.nValue = extra_amt
                extra_out.scriptPubKey = colored_p2pkh_script(extra_cid, pubkey_hash)
                tx.vout.append(extra_out)

        if change > 0:
            change_out = CTxOut()
            change_out.nValue = int(change * 10**8)
            change_out.scriptPubKey = p2pkh_script(pubkey_hash)
            tx.vout.append(change_out)

        return ToHex(tx)

    def wrap_tx_in_block(self, signed_tx_hex):
        tip_hash = self.nodes[0].getbestblockhash()
        tip_height = self.nodes[0].getblockcount()
        block_time = self.nodes[0].getblock(tip_hash)['time'] + 1

        coinbase = create_coinbase(tip_height + 1)
        block = create_block(int(tip_hash, 16), coinbase, block_time)

        tx = FromHex(CTransaction(), signed_tx_hex)
        tx.rehash()
        block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.hashImMerkleRoot = block.calc_immutable_merkle_root()
        block.solve(self.signblockprivkey)
        return block

    def assert_tx_rejected_mempool(self, signed_hex, expected_fragment):
        assert_raises_rpc_error(-26, expected_fragment,
                                self.nodes[0].sendrawtransaction, signed_hex)

    def assert_block_rejected_rpc(self, block, expected_fragment=None):
        result = self.nodes[0].submitblock(bytes_to_hex_str(block.serialize()))
        if expected_fragment:
            assert expected_fragment in (result or ''), \
                "Expected '%s' in submitblock result, got: %s" % (expected_fragment, result)
        else:
            assert result is not None, "Expected block to be rejected but submitblock returned None"

    def assert_block_rejected_p2p(self, block, method='block'):
        tip_before = self.nodes[0].getbestblockhash()
        if method == 'block':
            submit_via_p2p(self.peer, block)
        else:
            submit_via_compact_block(self.peer, block)
        # A block rejected with DoS(100) causes the node to ban and disconnect
        # the peer immediately.  Use wait_for_disconnect so we don't hang
        # waiting for a pong that will never arrive.  If the peer was NOT
        # disconnected, fall back to sync_with_ping to let processing finish.
        if not self.peer.is_connected:
            pass  # already disconnected
        else:
            try:
                self.peer.wait_for_disconnect(timeout=5)
            except Exception:
                # Not disconnected within 5 s — peer is still alive, use ping.
                self.peer.sync_with_ping()
        assert self.nodes[0].getbestblockhash() == tip_before, \
            "Block should have been rejected but tip moved"
        # If the peer was banned and disconnected, clear the ban list and
        # reconnect so subsequent P2P tests can proceed.
        if not self.peer.is_connected:
            self.nodes[0].clearbanned()
            self.peer = self.nodes[0].add_p2p_connection(
                P2PInterface(self.nodes[0].time_to_connect))

    def assert_block_rejected_loadblock(self, block):
        node = self.nodes[0]
        tip_before = node.getbestblockhash()

        import_file = os.path.join(node.datadir, 'invalid_block_test.dat')
        write_block_file(import_file, MagicBytes(), [block])

        self.stop_nodes()
        self.start_node(0, extra_args=['-loadblock=%s' % import_file])
        wait_until(lambda: self.nodes[0].getblockcount() >= 0, timeout=30)

        assert self.nodes[0].getbestblockhash() == tip_before, \
            "Invalid block must not be accepted via -loadblock"

        self.peer = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))
        self.log.info("  Confirmed: block rejected via -loadblock, tip unchanged")

    def assert_all_paths_reject(self, signed_hex, error):
        """Run all 5 rejection checks for a signed transaction."""
        self.assert_tx_rejected_mempool(signed_hex, error)
        block = self.wrap_tx_in_block(signed_hex)
        self.assert_block_rejected_rpc(block, error)
        self.assert_block_rejected_p2p(block, method='block')
        self.assert_block_rejected_p2p(block, method='compact')
        self.assert_block_rejected_loadblock(block)

    def tpc_change_output(self, tpc_utxo, pubkey_hash, fee=Decimal('0.001')):
        """Return a CTxOut carrying TPC change after deducting fee."""
        out = CTxOut()
        out.nValue = int((tpc_utxo['amount'] - fee) * 10**8)
        out.scriptPubKey = p2pkh_script(pubkey_hash)
        return out

    def colored_output(self, colored_utxo, colorid_bytes, pubkey_hash):
        """Return a CTxOut transferring all tokens from a colored UTXO.
        Token amounts in listunspent are raw integers, not TPC decimals."""
        out = CTxOut()
        out.nValue = colored_utxo['amount']
        out.scriptPubKey = colored_p2pkh_script(colorid_bytes, pubkey_hash)
        return out

    # ------------------------------------------------------------------ attack tests

    def test_duplicate_issuance_outputs_nft(self):
        """
        Attack: single tx issues two NFT outputs with the same colorId.
        NFT requires exactly one output per colorId per tx.
        Expected: bad-txns-nft-output-count

        Note: NON_REISSUABLE tokens may have multiple outputs with the same
        colorId in one issuance tx (split supply). This is valid because the
        colorId is unique to the spent UTXO and re-issuance in a future block
        is prevented by g_issued_colorids.
        """
        self.log.info("Test: duplicate issuance outputs [NFT]")
        node = self.nodes[0]
        utxo = self.get_spendable_utxo()
        prevout_bytes = serialize_outpoint(utxo['txid'], utxo['vout'])
        cid = color_id(NFT, prevout_bytes)
        pubkey_hash = self.get_pubkey_hash()

        raw = self.make_colored_issuance_tx(
            1, utxo, cid, pubkey_hash,
            extra_colored_outputs=[(cid, 1)]
        )
        signed = sign_and_get_hex(node, raw)
        self.assert_all_paths_reject(signed, "bad-txns-nft-output-count")

    def test_reissuance(self, token_type):
        """
        Attack: issue a token, then attempt to issue the same colorId again
        using a different TPC input (colorId mismatch → invalid-colorid).
        Expected: invalid-colorid
        """
        self.log.info("Test: re-issuance of same colorId [%s]" % TOKEN_TYPE_NAME[token_type])
        node = self.nodes[0]

        utxo1 = self.get_spendable_utxo()
        amt = default_amount(token_type)
        result = node.issuetoken(TOKEN_TYPE_NUM[token_type], amt, utxo1['txid'], utxo1['vout'])
        node.generate(1, self.signblockprivkey_wif)
        self.log.info("  Issued %s colorId: %s" % (TOKEN_TYPE_NAME[token_type], result['color']))

        prevout_bytes = serialize_outpoint(utxo1['txid'], utxo1['vout'])
        cid = color_id(token_type, prevout_bytes)

        utxo2 = self.get_spendable_utxo()
        pubkey_hash = self.get_pubkey_hash()
        raw = self.make_colored_issuance_tx(amt, utxo2, cid, pubkey_hash)
        signed = sign_and_get_hex(node, raw)
        self.assert_all_paths_reject(signed, "invalid-colorid")

    def test_nft_transfer_multiple_outputs(self):
        """
        Attack: spend an existing NFT UTXO and create two outputs with the
        same colorId. Expected: bad-txns-nft-output-count
        """
        self.log.info("Test: NFT transfer with two outputs [NFT]")
        node = self.nodes[0]

        utxo1 = self.get_spendable_utxo()
        result = node.issuetoken(TOKEN_TYPE_NUM[NFT], 1, utxo1['txid'], utxo1['vout'])
        node.generate(1, self.signblockprivkey_wif)

        nft_utxo = self.find_colored_utxo(result['color'])
        cid = hex_str_to_bytes(result['color'])

        tpc_utxo = self.get_spendable_utxo()
        pubkey_hash = self.get_pubkey_hash()

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(nft_utxo['txid'], 16), nft_utxo['vout']), b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'], 16), tpc_utxo['vout']), b'', 0xffffffff))
        for _ in range(2):
            out = CTxOut()
            out.nValue = 1
            out.scriptPubKey = colored_p2pkh_script(cid, pubkey_hash)
            tx.vout.append(out)
        tx.vout.append(self.tpc_change_output(tpc_utxo, pubkey_hash))

        signed = sign_and_get_hex(node, ToHex(tx))
        self.assert_all_paths_reject(signed, "bad-txns-nft-output-count")

    def test_nft_value_not_one(self):
        """
        Attack: issue an NFT output with nValue != 1. Expected: invalid-colorid
        """
        self.log.info("Test: NFT with nValue != 1 [NFT]")
        node = self.nodes[0]
        utxo = self.get_spendable_utxo()
        prevout_bytes = serialize_outpoint(utxo['txid'], utxo['vout'])
        cid = color_id(NFT, prevout_bytes)
        pubkey_hash = self.get_pubkey_hash()

        raw = self.make_colored_issuance_tx(5, utxo, cid, pubkey_hash)
        signed = sign_and_get_hex(node, raw)
        self.assert_all_paths_reject(signed, "invalid-colorid")

    def test_colored_output_in_coinbase(self, token_type):
        """
        Attack: coinbase transaction carries a colored output.
        CheckBlock rejects it with "bad-cb-issuetoken" before ConnectBlock is
        reached, so submitblock returns the generic "invalid" string (not the
        detailed rejection reason — BIP22ValidationResult is only reachable
        when the block passes CheckBlock and fails in ConnectBlock).
        """
        self.log.info("Test: colored output in coinbase [%s]" % TOKEN_TYPE_NAME[token_type])
        node = self.nodes[0]
        tip_hash = node.getbestblockhash()
        tip_height = node.getblockcount()
        block_time = node.getblock(tip_hash)['time'] + 1

        pubkey_hash = self.get_pubkey_hash()
        fake_cid = bytes([token_type]) + b'\x00' * 32

        coinbase = create_coinbase(tip_height + 1)
        colored_out = CTxOut()
        colored_out.nValue = 1
        colored_out.scriptPubKey = colored_p2pkh_script(fake_cid, pubkey_hash)
        coinbase.vout.append(colored_out)
        coinbase.rehash()

        block = create_block(int(tip_hash, 16), coinbase, block_time)
        block.solve(self.signblockprivkey)

        self.assert_block_rejected_rpc(block, "invalid")
        self.assert_block_rejected_p2p(block, method='block')
        self.assert_block_rejected_p2p(block, method='compact')
        self.assert_block_rejected_loadblock(block)

    # ------------------------------------------------------------------ multi-op tests

    def test_multi_op_issue_transfer_burn(self):
        """
        One issue + one transfer + one burn in a single transaction.

        vin:  TPC (issuance source + fee) | REISSUABLE (transfer) | NON_REISSUABLE (burn)
        vout: new NFT | REISSUABLE transfer | TPC change
        """
        self.log.info("Test multi-op: one issue + one transfer + one burn")
        node = self.nodes[0]
        pubkey_hash = self.get_pubkey_hash()

        r_result  = self.issue_token(REISSUABLE, 100)
        nr_result = self.issue_token(NON_REISSUABLE, 50)
        node.generate(1, self.signblockprivkey_wif)

        r_utxo  = self.find_colored_utxo(r_result['color'])
        nr_utxo = self.find_colored_utxo(nr_result['color'])
        tpc_utxo = self.get_spendable_utxo()

        nft_cid = color_id(NFT, serialize_outpoint(tpc_utxo['txid'], tpc_utxo['vout']))
        r_cid   = hex_str_to_bytes(r_result['color'])

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'], 16), tpc_utxo['vout']), b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(r_utxo['txid'],  16), r_utxo['vout']),  b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(nr_utxo['txid'], 16), nr_utxo['vout']), b'', 0xffffffff))

        nft_out = CTxOut()
        nft_out.nValue = 1
        nft_out.scriptPubKey = colored_p2pkh_script(nft_cid, pubkey_hash)
        tx.vout.append(nft_out)
        tx.vout.append(self.colored_output(r_utxo, r_cid, pubkey_hash))
        tx.vout.append(self.tpc_change_output(tpc_utxo, pubkey_hash))

        txid = node.sendrawtransaction(sign_and_get_hex(node, ToHex(tx)))
        node.generate(1, self.signblockprivkey_wif)
        self.log.info("  Accepted: %s" % txid)

        utxos = node.listunspent()
        assert any(u['token'] == bytes_to_hex_str(nft_cid) for u in utxos), "NFT not found"
        assert any(u['token'] == r_result['color'] for u in utxos), "REISSUABLE not found"
        assert not any(u['token'] == nr_result['color'] for u in utxos), "NON_REISSUABLE should be burned"

    def test_multi_op_two_issues(self):
        """
        Two different token types issued from the same TPC input in one transaction.
        The type byte distinguishes the colorIds even though both share the same prevout.

        vin:  TPC (issuance source + fee)
        vout: NON_REISSUABLE issuance | NFT issuance | TPC change
        """
        self.log.info("Test multi-op: two token types issued from one TPC input")
        node = self.nodes[0]
        pubkey_hash = self.get_pubkey_hash()

        tpc_utxo = self.get_spendable_utxo()
        prevout_bytes = serialize_outpoint(tpc_utxo['txid'], tpc_utxo['vout'])
        nr_cid  = color_id(NON_REISSUABLE, prevout_bytes)
        nft_cid = color_id(NFT, prevout_bytes)

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'], 16), tpc_utxo['vout']), b'', 0xffffffff))

        nr_out = CTxOut()
        nr_out.nValue = 100
        nr_out.scriptPubKey = colored_p2pkh_script(nr_cid, pubkey_hash)
        tx.vout.append(nr_out)

        nft_out = CTxOut()
        nft_out.nValue = 1
        nft_out.scriptPubKey = colored_p2pkh_script(nft_cid, pubkey_hash)
        tx.vout.append(nft_out)

        tx.vout.append(self.tpc_change_output(tpc_utxo, pubkey_hash))

        txid = node.sendrawtransaction(sign_and_get_hex(node, ToHex(tx)))
        node.generate(1, self.signblockprivkey_wif)
        self.log.info("  Accepted: %s" % txid)

        utxos = node.listunspent()
        assert any(u['token'] == bytes_to_hex_str(nr_cid)  for u in utxos), "NON_REISSUABLE not found"
        assert any(u['token'] == bytes_to_hex_str(nft_cid) for u in utxos), "NFT not found"

    def test_multi_op_two_issues_one_burn(self):
        """
        Two issuances + one burn in a single transaction.

        vin:  TPC (issuance source + fee) | REISSUABLE (burn)
        vout: NON_REISSUABLE issuance | NFT issuance | TPC change
        """
        self.log.info("Test multi-op: two issues + one burn")
        node = self.nodes[0]
        pubkey_hash = self.get_pubkey_hash()

        r_result = self.issue_token(REISSUABLE, 100)
        node.generate(1, self.signblockprivkey_wif)
        r_utxo = self.find_colored_utxo(r_result['color'])

        tpc_utxo = self.get_spendable_utxo()
        prevout_bytes = serialize_outpoint(tpc_utxo['txid'], tpc_utxo['vout'])
        nr_cid  = color_id(NON_REISSUABLE, prevout_bytes)
        nft_cid = color_id(NFT, prevout_bytes)

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'], 16), tpc_utxo['vout']), b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(r_utxo['txid'],  16), r_utxo['vout']),  b'', 0xffffffff))

        nr_out = CTxOut()
        nr_out.nValue = 100
        nr_out.scriptPubKey = colored_p2pkh_script(nr_cid, pubkey_hash)
        tx.vout.append(nr_out)

        nft_out = CTxOut()
        nft_out.nValue = 1
        nft_out.scriptPubKey = colored_p2pkh_script(nft_cid, pubkey_hash)
        tx.vout.append(nft_out)

        tx.vout.append(self.tpc_change_output(tpc_utxo, pubkey_hash))

        txid = node.sendrawtransaction(sign_and_get_hex(node, ToHex(tx)))
        node.generate(1, self.signblockprivkey_wif)
        self.log.info("  Accepted: %s" % txid)

        utxos = node.listunspent()
        assert any(u['token'] == bytes_to_hex_str(nr_cid)  for u in utxos), "NON_REISSUABLE not found"
        assert any(u['token'] == bytes_to_hex_str(nft_cid) for u in utxos), "NFT not found"
        assert not any(u['token'] == r_result['color'] for u in utxos), "REISSUABLE should be burned"

    def test_multi_op_two_transfers_one_burn(self):
        """
        Two transfers + one burn in a single transaction.

        vin:  TPC (fee) | REISSUABLE (transfer) | NON_REISSUABLE (transfer) | NFT (burn)
        vout: REISSUABLE transfer | NON_REISSUABLE transfer | TPC change
        """
        self.log.info("Test multi-op: two transfers + one burn")
        node = self.nodes[0]
        pubkey_hash = self.get_pubkey_hash()

        r_result  = self.issue_token(REISSUABLE, 100)
        nr_result = self.issue_token(NON_REISSUABLE, 50)
        nft_result = self.issue_token(NFT, 1)
        node.generate(1, self.signblockprivkey_wif)

        r_utxo   = self.find_colored_utxo(r_result['color'])
        nr_utxo  = self.find_colored_utxo(nr_result['color'])
        nft_utxo = self.find_colored_utxo(nft_result['color'])
        tpc_utxo = self.get_spendable_utxo()

        r_cid  = hex_str_to_bytes(r_result['color'])
        nr_cid = hex_str_to_bytes(nr_result['color'])

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'],  16), tpc_utxo['vout']),  b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(r_utxo['txid'],    16), r_utxo['vout']),    b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(nr_utxo['txid'],   16), nr_utxo['vout']),   b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(nft_utxo['txid'],  16), nft_utxo['vout']),  b'', 0xffffffff))

        tx.vout.append(self.colored_output(r_utxo,  r_cid,  pubkey_hash))
        tx.vout.append(self.colored_output(nr_utxo, nr_cid, pubkey_hash))
        tx.vout.append(self.tpc_change_output(tpc_utxo, pubkey_hash))

        txid = node.sendrawtransaction(sign_and_get_hex(node, ToHex(tx)))
        node.generate(1, self.signblockprivkey_wif)
        self.log.info("  Accepted: %s" % txid)

        utxos = node.listunspent()
        assert any(u['token'] == r_result['color']   for u in utxos), "REISSUABLE not found"
        assert any(u['token'] == nr_result['color']  for u in utxos), "NON_REISSUABLE not found"
        assert not any(u['token'] == nft_result['color'] for u in utxos), "NFT should be burned"

    def test_multi_op_one_transfer_two_burns(self):
        """
        One transfer + two burns in a single transaction.

        vin:  TPC (fee) | REISSUABLE (transfer) | NON_REISSUABLE (burn) | NFT (burn)
        vout: REISSUABLE transfer | TPC change
        """
        self.log.info("Test multi-op: one transfer + two burns")
        node = self.nodes[0]
        pubkey_hash = self.get_pubkey_hash()

        r_result   = self.issue_token(REISSUABLE, 100)
        nr_result  = self.issue_token(NON_REISSUABLE, 50)
        nft_result = self.issue_token(NFT, 1)
        node.generate(1, self.signblockprivkey_wif)

        r_utxo   = self.find_colored_utxo(r_result['color'])
        nr_utxo  = self.find_colored_utxo(nr_result['color'])
        nft_utxo = self.find_colored_utxo(nft_result['color'])
        tpc_utxo = self.get_spendable_utxo()

        r_cid = hex_str_to_bytes(r_result['color'])

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'],  16), tpc_utxo['vout']),  b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(r_utxo['txid'],    16), r_utxo['vout']),    b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(nr_utxo['txid'],   16), nr_utxo['vout']),   b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(nft_utxo['txid'],  16), nft_utxo['vout']),  b'', 0xffffffff))

        tx.vout.append(self.colored_output(r_utxo, r_cid, pubkey_hash))
        tx.vout.append(self.tpc_change_output(tpc_utxo, pubkey_hash))

        txid = node.sendrawtransaction(sign_and_get_hex(node, ToHex(tx)))
        node.generate(1, self.signblockprivkey_wif)
        self.log.info("  Accepted: %s" % txid)

        utxos = node.listunspent()
        assert any(u['token'] == r_result['color']        for u in utxos), "REISSUABLE not found"
        assert not any(u['token'] == nr_result['color']   for u in utxos), "NON_REISSUABLE should be burned"
        assert not any(u['token'] == nft_result['color']  for u in utxos), "NFT should be burned"

    def test_multi_op_three_burns(self):
        """
        Three token burns in a single transaction — no colored outputs at all.

        vin:  TPC (fee) | REISSUABLE (burn) | NON_REISSUABLE (burn) | NFT (burn)
        vout: TPC change only
        """
        self.log.info("Test multi-op: three burns")
        node = self.nodes[0]
        pubkey_hash = self.get_pubkey_hash()

        r_result   = self.issue_token(REISSUABLE, 100)
        nr_result  = self.issue_token(NON_REISSUABLE, 50)
        nft_result = self.issue_token(NFT, 1)
        node.generate(1, self.signblockprivkey_wif)

        r_utxo   = self.find_colored_utxo(r_result['color'])
        nr_utxo  = self.find_colored_utxo(nr_result['color'])
        nft_utxo = self.find_colored_utxo(nft_result['color'])
        tpc_utxo = self.get_spendable_utxo()

        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'],  16), tpc_utxo['vout']),  b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(r_utxo['txid'],    16), r_utxo['vout']),    b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(nr_utxo['txid'],   16), nr_utxo['vout']),   b'', 0xffffffff))
        tx.vin.append(CTxIn(COutPoint(int(nft_utxo['txid'],  16), nft_utxo['vout']),  b'', 0xffffffff))

        tx.vout.append(self.tpc_change_output(tpc_utxo, pubkey_hash))

        txid = node.sendrawtransaction(sign_and_get_hex(node, ToHex(tx)))
        node.generate(1, self.signblockprivkey_wif)
        self.log.info("  Accepted: %s" % txid)

        utxos = node.listunspent()
        assert not any(u['token'] == r_result['color']   for u in utxos), "REISSUABLE should be burned"
        assert not any(u['token'] == nr_result['color']  for u in utxos), "NON_REISSUABLE should be burned"
        assert not any(u['token'] == nft_result['color'] for u in utxos), "NFT should be burned"

    def test_colored_script_in_genesis_coinbase(self):
        """
        Attack: a block (built on the genesis tip at height 1) whose coinbase
        carries a colored output whose colorId is derived from the genesis
        coinbase outpoint.

        Two independent defences apply:
          1. CheckBlock: "coinbase cannot issue tokens" → rejects before ConnectBlock.
          2. ConnectBlock genesis special-case: genesis transactions are never
             connected, so even a genesis-coinbase-derived colorId would never be
             registered in g_issued_colorids.

        We verify all block-submission paths reject the block.
        Note: sendrawtransaction is not tested because coinbase txs cannot enter
        the mempool by design (rejected with "coinbase").
        """
        self.log.info("Test: colored script in genesis coinbase (all token types)")
        node = self.nodes[0]

        # Fetch the genesis coinbase outpoint to use its txid as the colorId source.
        genesis_hash = node.getblockhash(0)
        genesis_block = node.getblock(genesis_hash)
        genesis_coinbase_txid = genesis_block['tx'][0]

        tip_hash   = node.getbestblockhash()
        tip_height = node.getblockcount()
        block_time = node.getblock(tip_hash)['time'] + 1
        pubkey_hash = self.get_pubkey_hash()

        for token_type in [REISSUABLE, NON_REISSUABLE, NFT]:
            self.log.info("  token type: %s" % TOKEN_TYPE_NAME[token_type])

            # ColorId derived from genesis coinbase outpoint
            genesis_prevout_bytes = serialize_outpoint(genesis_coinbase_txid, 0)
            cid = color_id(token_type, genesis_prevout_bytes)

            coinbase = create_coinbase(tip_height + 1)
            colored_out = CTxOut()
            colored_out.nValue = default_amount(token_type)
            colored_out.scriptPubKey = colored_p2pkh_script(cid, pubkey_hash)
            coinbase.vout.append(colored_out)
            coinbase.rehash()

            block = create_block(int(tip_hash, 16), coinbase, block_time)
            block.solve(self.signblockprivkey)

            # CheckBlock fires "bad-cb-issuetoken" → submitblock returns "invalid"
            self.assert_block_rejected_rpc(block)
            self.assert_block_rejected_p2p(block, method='block')
            self.assert_block_rejected_p2p(block, method='compact')
            self.assert_block_rejected_loadblock(block)

    def test_colored_issue_from_genesis_coinbase(self):
        """
        Attack: spend the genesis coinbase outpoint in a regular (non-coinbase)
        transaction to issue colored tokens.

        The genesis coinbase UTXO is intentionally unspendable — it is never
        added to the UTXO set, so any TX referencing it as an input is rejected
        with "Missing inputs" at the mempool level and at the block level.

        Tested for NON_REISSUABLE and NFT (colorId derivation depends on prevout).
        """
        self.log.info("Test: colored issue using genesis coinbase outpoint as input")
        node = self.nodes[0]

        genesis_hash = node.getblockhash(0)
        genesis_block = node.getblock(genesis_hash)
        genesis_coinbase_txid = genesis_block['tx'][0]
        genesis_prevout_bytes = serialize_outpoint(genesis_coinbase_txid, 0)

        pubkey_hash = self.get_pubkey_hash()

        for token_type in [NON_REISSUABLE, NFT]:
            self.log.info("  token type: %s" % TOKEN_TYPE_NAME[token_type])
            cid = color_id(token_type, genesis_prevout_bytes)
            tpc_utxo = self.get_spendable_utxo()

            tx = CTransaction()
            # Input 0: genesis coinbase outpoint (unspendable — not in UTXO set)
            tx.vin.append(CTxIn(COutPoint(int(genesis_coinbase_txid, 16), 0), b'', 0xffffffff))
            # Input 1: regular TPC UTXO for fee coverage
            tx.vin.append(CTxIn(COutPoint(int(tpc_utxo['txid'], 16), tpc_utxo['vout']), b'', 0xffffffff))

            colored_out = CTxOut()
            colored_out.nValue = default_amount(token_type)
            colored_out.scriptPubKey = colored_p2pkh_script(cid, pubkey_hash)
            tx.vout.append(colored_out)
            tx.vout.append(self.tpc_change_output(tpc_utxo, pubkey_hash))

            raw_hex = ToHex(tx)

            # Mempool: rejected with "Missing inputs" (genesis coinbase UTXO doesn't exist)
            assert_raises_rpc_error(-25, "Missing inputs",
                                    node.sendrawtransaction, raw_hex)

            # Block-level: wrap the unsigned TX in a block and verify rejection.
            # The block fails at ConnectBlock because the genesis coinbase input is missing.
            tx.rehash()
            tip_hash   = node.getbestblockhash()
            tip_height = node.getblockcount()
            block_time = node.getblock(tip_hash)['time'] + 1
            coinbase   = create_coinbase(tip_height + 1)
            block = create_block(int(tip_hash, 16), coinbase, block_time)
            block.vtx.append(tx)
            block.hashMerkleRoot    = block.calc_merkle_root()
            block.hashImMerkleRoot  = block.calc_immutable_merkle_root()
            block.solve(self.signblockprivkey)

            self.assert_block_rejected_rpc(block)
            self.assert_block_rejected_p2p(block, method='block')
            self.assert_block_rejected_p2p(block, method='compact')
            self.assert_block_rejected_loadblock(block)

    # ------------------------------------------------------------------ runner

    def run_test(self):
        node = self.nodes[0]
        node.generate(50, self.signblockprivkey_wif)

        # Attack scenario 1: duplicate issuance outputs — NFT only.
        # REISSUABLE: multiple issuances with the same colorId are valid by design.
        # NON_REISSUABLE: multiple outputs with the same colorId in one tx are also
        # valid (split supply); re-issuance in a future block is prevented globally.
        self.test_duplicate_issuance_outputs_nft()
        node.generate(1, self.signblockprivkey_wif)

        # Attack scenario 2: re-issuance — NON_REISSUABLE and NFT only
        for token_type in [NON_REISSUABLE, NFT]:
            self.test_reissuance(token_type)
            node.generate(1, self.signblockprivkey_wif)

        # Attack scenario 3 & 4: NFT-specific failures
        self.test_nft_transfer_multiple_outputs()
        node.generate(1, self.signblockprivkey_wif)

        self.test_nft_value_not_one()
        node.generate(1, self.signblockprivkey_wif)

        # Attack scenario 5: colored output in coinbase — all token types
        for token_type in [REISSUABLE, NON_REISSUABLE, NFT]:
            self.test_colored_output_in_coinbase(token_type)

        # Attack scenario 6: colored script in genesis coinbase
        self.test_colored_script_in_genesis_coinbase()

        # Attack scenario 7: colored issue using genesis coinbase outpoint
        self.test_colored_issue_from_genesis_coinbase()
        node.generate(1, self.signblockprivkey_wif)

        # Valid multi-op scenarios
        self.test_multi_op_issue_transfer_burn()
        self.test_multi_op_two_issues()
        self.test_multi_op_two_issues_one_burn()
        self.test_multi_op_two_transfers_one_burn()
        self.test_multi_op_one_transfer_two_burns()
        self.test_multi_op_three_burns()


if __name__ == '__main__':
    ColoredCoinVulnTest().main()
