#!/usr/bin/env python3
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
'''
Test all token type - REISSUABLE, NON-REISSUABLE, NFT

Setup
coinbaseTx 1 - 10

Create TXs
TxSuccess1 - coinbaseTx1 - issue 100 REISSUABLE  + 30     (UTXO-1,2)
TxSuccess2 - (UTXO-2)    - issue 100 NON-REISSUABLE       (UTXO-3)
TxSuccess3 - coinbaseTx2 - issue 1 NFT                    (UTXO-4)

TxFailure4 - (UTXO-1)    - split REISSUABLE - 25 + 75
           - (UTXO-3)    - split NON-REISSUABLE - 40 + 60
           - coinbaseTx3 - issue 100 REISSUABLE

TxSuccess4 - (UTXO-1)    - split REISSUABLE - 25 + 75     (UTXO-5,6)
           - (UTXO-3)    - split NON-REISSUABLE - 40 + 60 (UTXO-7,8)

TxFailure5 - (UTXO-6)    - split REISSUABLE(75)
           - (UTXO-7)    - split NON-REISSUABLE(40)
           - (UTXO-4)    - split NFT
           - coinbaseTx4

TxSuccess5 - (UTXO-6)    - split REISSUABLE(75)           (UTXO-10,11)
           - (UTXO-7)    - split NON-REISSUABLE(40)       (UTXO-12)
           - (UTXO-4)    - transfer NFT                   (UTXO-13)
           - coinbaseTx4

TxFailure6 - (UTXO-11)   - transfer REISSUABLE(40)
           - (UTXO-8)    - burn NON-REISSUABLE(60)
           - (UTXO-13)   - transfer NFT
           - coinbaseTx5 - issue 1000 REISSUABLE1, change

TxSuccess6 - (UTXO-11)   - transfer REISSUABLE(40)        (UTXO-14)
           - (UTXO-8)    - burn NON-REISSUABLE(60)        (UTXO-15)*
           - (UTXO-13)   - transfer NFT                   (UTXO-16)
           - coinbaseTx5 - change

TxSuccess7 - coinbaseTx5 - issue 1000 REISSUABLE1         (UTXO-17)

TxFailure7 - (UTXO-9,14) - aggregate REISSUABLE(25 + 40)
           - (UTXO-12)   - burn NON-REISSUABLE(20)

txSuccess8 - (UTXO-9,14) - aggregate REISSUABLE(25 + 40) x
           - (UTXO-12)   - burn NON-REISSUABLE(20)        *
           - coinbase[6]

TxFailure8 - (UTXO-15)   - convert REISSUABLE to NON-REISSUABLE

Consensus rule / attack-scenario tests (run after the above):
  1. Duplicate outputs in a single issuance tx (same colorId, two outputs)
     — NFT only (bad-txns-nft-output-count)
  2. Re-issuance of an already-issued colorId
     — NON_REISSUABLE, NFT  (REISSUABLE allows re-issuance by design)
  3. NFT transfer with two outputs of same colorId (bad-txns-nft-output-count)
     — NFT only
  4. NFT with nValue != 1 (value inflation)
     — NFT only
  5. Colored output in coinbase (consensus bypass)
     — REISSUABLE, NON_REISSUABLE, NFT
  6. Colored script in genesis coinbase
     — REISSUABLE, NON_REISSUABLE, NFT
  7. Colored issue using genesis coinbase outpoint as input
     — NON_REISSUABLE, NFT

Valid multi-op scenarios (all in one transaction):
  8.  One issue + one transfer + one burn
  9.  Two token types issued from the same TPC input
  10. Two issues + one burn
  11. Two transfers + one burn
  12. One transfer + two burns
  13. Three burns

Each attack scenario is verified across all submission paths:
  - RPC sendrawtransaction (mempool acceptance)
  - RPC submitblock (block validation)
  - P2P msg_block (direct block announcement)
  - P2P msg_cmpctblock (compact block announcement)
  - -loadblock file (block file import on restart)
'''

import os
import struct
from decimal import Decimal

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import (
    CTransaction, CTxIn, CTxOut, COutPoint, msg_tx, COIN, sha256, msg_block,
    FromHex, HeaderAndShortIDs, msg_cmpctblock, ToHex,
)
from test_framework.key import CECKey
from test_framework.schnorr import Schnorr
from test_framework.mininode import P2PDataStore, P2PInterface, mininode_lock
from test_framework.timeout_config import TAPYRUSD_MIN_TIMEOUT
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal, assert_raises_rpc_error,
    hex_str_to_bytes, bytes_to_hex_str,
    MagicBytes, wait_until,
)
from test_framework.script import (
    CScript, OP_COLOR, hash160,
    OP_DUP, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG,
    SignatureHash, SIGHASH_ALL, OP_EQUAL,
)

REISSUABLE     = 0xc1
NON_REISSUABLE = 0xc2
NFT            = 0xc3

TOKEN_TYPE_NUM  = {REISSUABLE: 1, NON_REISSUABLE: 2, NFT: 3}
TOKEN_TYPE_NAME = {REISSUABLE: 'REISSUABLE', NON_REISSUABLE: 'NON_REISSUABLE', NFT: 'NFT'}


def colorIdReissuable(script):
    return b'\xc1' + sha256(script)

def colorIdNonReissuable(utxo):
    return b'\xc2'+ sha256(utxo)

def colorIdNFT(utxo):
    return b'\xc3'+ sha256(utxo)

def CP2PHK_script(colorId, pubkey):
    pubkeyhash = hash160(hex_str_to_bytes(pubkey))
    return CScript([colorId, OP_COLOR, OP_DUP, OP_HASH160, pubkeyhash, OP_EQUALVERIFY, OP_CHECKSIG ])

def CP2SH_script(colorId, redeemScr):
    redeemScrhash = hash160(hex_str_to_bytes(redeemScr))
    return CScript([colorId, OP_COLOR, OP_HASH160, redeemScrhash, OP_EQUAL])

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
    # Prefill all transactions so the node can reconstruct without issuing
    # getblocktxn (attack transactions are not in the mempool).
    cmpct.initialize_from_block(block, prefill_list=list(range(len(block.vtx))))
    peer.send_message(msg_cmpctblock(cmpct.to_p2p()))

def write_block_file(path, magic, blocks):
    with open(path, 'wb') as f:
        for block in blocks:
            raw = block.serialize()
            f.write(magic)
            f.write(struct.pack('<I', len(raw)))
            f.write(raw)

def test_transaction_acceptance(node, tx, accepted, reason=None, expect_disconnect=False):
    """Send a transaction to the node and check that it's accepted to the mempool.

    If expect_disconnect is True the node is expected to ban the peer (DoS) after
    rejecting the transaction.  The connection is re-established automatically so
    subsequent calls keep working.
    """
    with mininode_lock:
        node.p2p.last_message = {}
    tx_message = msg_tx(tx)
    node.p2p.send_message(tx_message)
    if expect_disconnect:
        node.p2p.wait_for_disconnect()
    else:
        node.p2p.sync_with_ping()
    assert_equal(tx.hashMalFix in node.getrawmempool(), accepted)
    if (reason is not None and not accepted):
        # Check the rejection reason as well.
        with mininode_lock:
            assert_equal(node.p2p.last_message["reject"].reason, reason)
    if expect_disconnect:
        # Reconnect so subsequent test_transaction_acceptance calls keep working.
        node.p2ps.clear()
        node.add_p2p_connection(P2PDataStore(node.time_to_connect))
        node.p2p.wait_for_getheaders(timeout=TAPYRUSD_MIN_TIMEOUT)

class ColoredCoinTest(BitcoinTestFramework):
    def set_test_params(self):
        self.pubkeys = ["025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3",
        "03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc"]

        privkeystr = ["67ae3f5bfb3464b9704d7bd3a134401cc80c3a172240ebfca9f1e40f51bb6d37",
        "dbb9d19637018267268dfc2cc7aec07e7217c1a2d6733e1184a0909273bf078b"]

        self.privkeys = []
        for key in privkeystr :
            ckey = CECKey()
            ckey.set_secretbytes(bytes.fromhex(key))
            self.privkeys.append(ckey)

        self.coinbase_key = CECKey()
        self.coinbase_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))
        self.coinbase_pubkey = self.coinbase_key.get_pubkey()

        self.schnorr_key = Schnorr()
        self.schnorr_key.set_secretbytes(bytes.fromhex("12b004fff7f4b69ef8650e767f18f11ede158148b425660723b9f9a66e61f747"))

        self.num_nodes = 1
        self.setup_clean_chain = True

    # ------------------------------------------------------------------ helpers (wallet-based)

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

    def wrap_tx_in_block(self, signed_tx_hex, time_offset=0):
        tip_hash = self.nodes[0].getbestblockhash()
        tip_height = self.nodes[0].getblockcount()
        block_time = self.nodes[0].getblock(tip_hash)['time'] + 1 + time_offset

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
            # DoS(100) hits the ban threshold (DEFAULT_BANSCORE_THRESHOLD=100),
            # so the node disconnects the peer after sending msg_reject.
            # wait_for_disconnect confirms the rejection fired and the peer was
            # penalised; we cannot use sync_with_ping because the pong never
            # arrives on a closed connection.
            self.peer.wait_for_disconnect()
        else:
            submit_via_compact_block(self.peer, block)
            # For compact blocks mapBlockSource is inserted with
            # second.second=false (BIP 152: don't penalise relayers before full
            # validation), so Misbehaving() is intentionally suppressed and the
            # peer is not disconnected.  sync_with_ping is safe here.
            self.peer.sync_with_ping()

        assert self.nodes[0].getbestblockhash() == tip_before, \
            "Block should have been rejected but tip moved"

        # Reconnect for subsequent tests.
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
        """Run all 5 rejection checks for a signed transaction.

        Each block-level path gets its own fresh block (different time_offset)
        so that one path's submission (which marks the block BLOCK_FAILED_VALID
        on disk) does not cause subsequent paths to hit the fAlreadyHave
        early-return in AcceptBlock, which would suppress the DoS scoring.
        """
        self.assert_tx_rejected_mempool(signed_hex, error)
        self.assert_block_rejected_rpc(self.wrap_tx_in_block(signed_hex, time_offset=0), error)
        self.assert_block_rejected_p2p(self.wrap_tx_in_block(signed_hex, time_offset=1), method='block')
        self.assert_block_rejected_p2p(self.wrap_tx_in_block(signed_hex, time_offset=2), method='compact')
        self.assert_block_rejected_loadblock(self.wrap_tx_in_block(signed_hex, time_offset=3))

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
        using a different TPC input (colorId mismatch → bad-txns-token-noinput).
        Expected: bad-txns-token-noinput
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
        self.assert_all_paths_reject(signed, "bad-txns-token-noinput")

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
        Attack: issue an NFT output with nValue != 1. Expected: bad-txns-nft-amount
        """
        self.log.info("Test: NFT with nValue != 1 [NFT]")
        node = self.nodes[0]
        utxo = self.get_spendable_utxo()
        prevout_bytes = serialize_outpoint(utxo['txid'], utxo['vout'])
        cid = color_id(NFT, prevout_bytes)
        pubkey_hash = self.get_pubkey_hash()

        raw = self.make_colored_issuance_tx(5, utxo, cid, pubkey_hash)
        signed = sign_and_get_hex(node, raw)
        self.assert_all_paths_reject(signed, "bad-txns-nft-amount")

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

        def make_block(time_offset=0):
            cb = create_coinbase(tip_height + 1)
            cout = CTxOut()
            cout.nValue = 1
            cout.scriptPubKey = colored_p2pkh_script(fake_cid, pubkey_hash)
            cb.vout.append(cout)
            cb.rehash()
            b = create_block(int(tip_hash, 16), cb, block_time + time_offset)
            b.solve(self.signblockprivkey)
            return b

        self.assert_block_rejected_rpc(make_block(0), "invalid")
        self.assert_block_rejected_p2p(make_block(1), method='block')
        self.assert_block_rejected_p2p(make_block(2), method='compact')
        self.assert_block_rejected_loadblock(make_block(3))

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

        genesis_prevout_bytes = serialize_outpoint(genesis_coinbase_txid, 0)

        for token_type in [REISSUABLE, NON_REISSUABLE, NFT]:
            self.log.info("  token type: %s" % TOKEN_TYPE_NAME[token_type])

            cid = color_id(token_type, genesis_prevout_bytes)

            def make_block(time_offset=0, _cid=cid, _token_type=token_type):
                cb = create_coinbase(tip_height + 1)
                cout = CTxOut()
                cout.nValue = default_amount(_token_type)
                cout.scriptPubKey = colored_p2pkh_script(_cid, pubkey_hash)
                cb.vout.append(cout)
                cb.rehash()
                b = create_block(int(tip_hash, 16), cb, block_time + time_offset)
                b.solve(self.signblockprivkey)
                return b

            # CheckBlock fires "bad-cb-issuetoken" → submitblock returns "invalid"
            self.assert_block_rejected_rpc(make_block(0))
            self.assert_block_rejected_p2p(make_block(1), method='block')
            self.assert_block_rejected_p2p(make_block(2), method='compact')
            self.assert_block_rejected_loadblock(make_block(3))

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
            # Each path gets its own block (different nTime) so that the first
            # submission's BLOCK_FAILED_VALID mark does not cause fAlreadyHave
            # for subsequent paths, which would suppress DoS scoring.
            tx.rehash()
            tip_hash   = node.getbestblockhash()
            tip_height = node.getblockcount()
            base_time  = node.getblock(tip_hash)['time'] + 1

            def make_block(time_offset=0, _tx=tx):
                cb = create_coinbase(tip_height + 1)
                b = create_block(int(tip_hash, 16), cb, base_time + time_offset)
                b.vtx.append(_tx)
                b.hashMerkleRoot   = b.calc_merkle_root()
                b.hashImMerkleRoot = b.calc_immutable_merkle_root()
                b.solve(self.signblockprivkey)
                return b

            self.assert_block_rejected_rpc(make_block(0))
            self.assert_block_rejected_p2p(make_block(1), method='block')
            self.assert_block_rejected_p2p(make_block(2), method='compact')
            self.assert_block_rejected_loadblock(make_block(3))

    # ------------------------------------------------------------------ reindex tests

    def test_reindex_chainstate_colorid_set(self):
        """
        -reindex-chainstate preserves pblocktree but wipes the chainstate.
        Before the fix, LoadIssuedColorIds pre-populated g_issued_colorids
        with stale DB_ISSUED_COLORID entries, causing bad-txns-colorid-already-issued
        when the issuance block was re-connected.
        After the fix, ClearIssuedColorIds() wipes those entries first so
        re-connection succeeds and g_issued_colorids is rebuilt correctly.
        """
        self.log.info("Test: -reindex-chainstate rebuilds g_issued_colorids correctly")
        node = self.nodes[0]

        nr_result  = self.issue_token(NON_REISSUABLE)
        nft_result = self.issue_token(NFT)
        node.generate(1, self.signblockprivkey_wif)
        expected_height = node.getblockcount()

        self.stop_nodes()
        self.start_node(0, extra_args=['-reindex-chainstate'])
        wait_until(lambda: self.nodes[0].getblockcount() == expected_height, timeout=120)
        self.log.info("  -reindex-chainstate complete, height=%d" % expected_height)

        self.peer = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))

        # Both colored UTXOs must survive the chainstate rebuild.
        utxos = node.listunspent()
        assert any(u['token'] == nr_result['color']  for u in utxos), \
            "NON_REISSUABLE UTXO missing after -reindex-chainstate"
        assert any(u['token'] == nft_result['color'] for u in utxos), \
            "NFT UTXO missing after -reindex-chainstate"

        # g_issued_colorids must be correctly populated: new issuances must work ...
        new_nr = self.issue_token(NON_REISSUABLE)
        node.generate(1, self.signblockprivkey_wif)
        assert any(u['token'] == new_nr['color'] for u in node.listunspent()), \
            "NON_REISSUABLE issuance failed after -reindex-chainstate"

        # ... and a re-issuance attempt using the original colorId but a different
        # TPC input must be rejected (bad-txns-token-noinput because the source
        # UTXO is spent, so no vin in the new tx matches the colorId derivation).
        nr_cid_bytes = hex_str_to_bytes(nr_result['color'])
        tpc_utxo     = self.get_spendable_utxo()
        pubkey_hash  = self.get_pubkey_hash()
        raw    = self.make_colored_issuance_tx(100, tpc_utxo, nr_cid_bytes, pubkey_hash)
        signed = sign_and_get_hex(node, raw)
        self.assert_tx_rejected_mempool(signed, "bad-txns-token-noinput")

    def test_reindex_colorid_set(self):
        """
        -reindex wipes pblocktree entirely, so stale DB_ISSUED_COLORID entries
        never arise and g_issued_colorids is always rebuilt from scratch.
        This test guards against regressions in that path.
        """
        self.log.info("Test: -reindex rebuilds g_issued_colorids correctly")
        node = self.nodes[0]

        nr_result  = self.issue_token(NON_REISSUABLE)
        nft_result = self.issue_token(NFT)
        node.generate(1, self.signblockprivkey_wif)
        expected_height = node.getblockcount()

        self.stop_nodes()
        self.start_node(0, extra_args=['-reindex'])
        wait_until(lambda: self.nodes[0].getblockcount() == expected_height, timeout=120)
        self.log.info("  -reindex complete, height=%d" % expected_height)

        self.peer = self.nodes[0].add_p2p_connection(P2PInterface(self.nodes[0].time_to_connect))

        utxos = node.listunspent()
        assert any(u['token'] == nr_result['color']  for u in utxos), \
            "NON_REISSUABLE UTXO missing after -reindex"
        assert any(u['token'] == nft_result['color'] for u in utxos), \
            "NFT UTXO missing after -reindex"

        new_nft = self.issue_token(NFT)
        node.generate(1, self.signblockprivkey_wif)
        assert any(u['token'] == new_nft['color'] for u in node.listunspent()), \
            "NFT issuance failed after -reindex"

        # Re-issuance of the original NFT colorId from a different input must fail.
        nft_cid_bytes = hex_str_to_bytes(nft_result['color'])
        tpc_utxo      = self.get_spendable_utxo()
        pubkey_hash   = self.get_pubkey_hash()
        raw    = self.make_colored_issuance_tx(1, tpc_utxo, nft_cid_bytes, pubkey_hash)
        signed = sign_and_get_hex(node, raw)
        self.assert_tx_rejected_mempool(signed, "bad-txns-token-noinput")

    # ------------------------------------------------------------------ main test runner

    def run_test(self):
        node = self.nodes[0]  # convenience reference to the node
        self.address = node.getnewaddress()
        node.add_p2p_connection(P2PDataStore(node.time_to_connect))
        node.p2p.wait_for_getheaders(timeout=TAPYRUSD_MIN_TIMEOUT)
        self.address = self.nodes[0].getnewaddress()

        self.log.info("Test starting...")

        #generate 10 blocks for coinbase outputs
        coinbase_txs = []
        for i in range(1, 10):
            height = node.getblockcount() + 1
            coinbase_tx = create_coinbase(height, self.coinbase_pubkey)
            coinbase_txs.append(coinbase_tx)
            tip = node.getbestblockhash()
            block_time = node.getblockheader(tip)["mediantime"] + 1
            block = create_block(int(tip, 16), coinbase_tx, block_time)
            block.solve(self.signblockprivkey)
            tip = block.hash

            node.p2p.send_and_ping(msg_block(block))
            assert_equal(node.getbestblockhash(), tip)

        change_script = CScript([self.coinbase_pubkey, OP_CHECKSIG])
        burn_script = CScript([hex_str_to_bytes(self.pubkeys[1]), OP_CHECKSIG])

        #TxSuccess1 - coinbaseTx1 - issue 100 REISSUABLE  + 30     (UTXO-1,2)
        colorId_reissuable = colorIdReissuable(coinbase_txs[0].vout[0].scriptPubKey)
        script_reissuable = CP2PHK_script(colorId = colorId_reissuable, pubkey = self.pubkeys[0])
        script_transfer_reissuable = CP2PHK_script(colorId = colorId_reissuable, pubkey = self.pubkeys[1])

        txSuccess1 = CTransaction()
        txSuccess1.vin.append(CTxIn(COutPoint(coinbase_txs[0].malfixsha256, 0), b""))
        txSuccess1.vout.append(CTxOut(100, script_reissuable))
        txSuccess1.vout.append(CTxOut(30 * COIN, CScript([self.coinbase_pubkey, OP_CHECKSIG])))
        sig_hash, err = SignatureHash(coinbase_txs[0].vout[0].scriptPubKey, txSuccess1, 0, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'  # 0x1 is SIGHASH_ALL
        txSuccess1.vin[0].scriptSig = CScript([signature])
        txSuccess1.rehash()

        test_transaction_acceptance(node, txSuccess1, accepted=True)
        tx_info = node.getrawtransaction(txSuccess1.hashMalFix, 1)
        assert_equal(tx_info['vout'][0]['token'], bytes_to_hex_str(colorId_reissuable))
        assert_equal(tx_info['vout'][0]['value'], 100)

        #TxSuccess2 - (UTXO-2)    - issue 100 NON-REISSUABLE       (UTXO-3)
        colorId_nonreissuable = colorIdNonReissuable(COutPoint(txSuccess1.malfixsha256, 1).serialize())
        script_nonreissuable = CP2PHK_script(colorId = colorId_nonreissuable, pubkey = self.pubkeys[0])
        script_transfer_nonreissuable = CP2PHK_script(colorId = colorId_nonreissuable, pubkey = self.pubkeys[1])

        txSuccess2 = CTransaction()
        txSuccess2.vin.append(CTxIn(COutPoint(txSuccess1.malfixsha256, 1), b""))
        txSuccess2.vout.append(CTxOut(100, script_nonreissuable))
        sig_hash, err = SignatureHash(txSuccess1.vout[1].scriptPubKey, txSuccess2, 0, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess2.vin[0].scriptSig = CScript([signature])
        txSuccess2.rehash()

        test_transaction_acceptance(node, txSuccess2, accepted=True)
        tx_info = node.getrawtransaction(txSuccess2.hashMalFix, 1)
        assert_equal(tx_info['vout'][0]['token'], bytes_to_hex_str(colorId_nonreissuable))
        assert_equal(tx_info['vout'][0]['value'], 100)

        #TxSuccess3 - coinbaseTx2 - issue 1 NFT                    (UTXO-4)
        colorId_nft = colorIdNFT(COutPoint(coinbase_txs[1].malfixsha256, 0).serialize())
        script_nft = CP2PHK_script(colorId = colorId_nft, pubkey = self.pubkeys[0])
        script_transfer_nft = CP2PHK_script(colorId = colorId_nft, pubkey = self.pubkeys[0])

        txSuccess3 = CTransaction()
        txSuccess3.vin.append(CTxIn(COutPoint(coinbase_txs[1].malfixsha256, 0), b""))
        txSuccess3.vout.append(CTxOut(1, script_nft))
        sig_hash, err = SignatureHash(coinbase_txs[1].vout[0].scriptPubKey, txSuccess3, 0, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess3.vin[0].scriptSig = CScript([signature])
        txSuccess3.rehash()

        test_transaction_acceptance(node, txSuccess3, accepted=True)
        tx_info = node.getrawtransaction(txSuccess3.hashMalFix, 1)
        assert_equal(tx_info['vout'][0]['token'], bytes_to_hex_str(colorId_nft))
        assert_equal(tx_info['vout'][0]['value'], 1)

        #TxFailure4 - (UTXO-1)    - split REISSUABLE - 25 + 75     (UTXO-5,6)
        #           - (UTXO-3)    - split NON-REISSUABLE - 40 + 60 (UTXO-7,8)
        #           - coinbaseTx3 - issue 100 REISSUABLE           (UTXO-9)
        TxFailure4 = CTransaction()
        TxFailure4.vin.append(CTxIn(COutPoint(txSuccess1.malfixsha256, 0), b""))
        TxFailure4.vin.append(CTxIn(COutPoint(txSuccess2.malfixsha256, 0), b""))
        TxFailure4.vin.append(CTxIn(COutPoint(coinbase_txs[2].malfixsha256, 0), b""))
        TxFailure4.vout.append(CTxOut(25, script_reissuable))
        TxFailure4.vout.append(CTxOut(75, script_reissuable))
        TxFailure4.vout.append(CTxOut(40, script_nonreissuable))
        TxFailure4.vout.append(CTxOut(60, script_nonreissuable))
        TxFailure4.vout.append(CTxOut(100, script_reissuable))
        sig_hash, err = SignatureHash(txSuccess1.vout[0].scriptPubKey, TxFailure4, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure4.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess2.vout[0].scriptPubKey, TxFailure4, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure4.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[2].vout[0].scriptPubKey, TxFailure4, 2, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        TxFailure4.vin[2].scriptSig = CScript([signature])
        TxFailure4.rehash()

        test_transaction_acceptance(node, TxFailure4, accepted=False, reason=b"bad-txns-token-balance")

        #TxSuccess4 - (UTXO-1)    - split REISSUABLE - 25 + 75     (UTXO-5,6)
        #           - (UTXO-3)    - split NON-REISSUABLE - 40 + 60 (UTXO-7,8)
        txSuccess4 = CTransaction()
        txSuccess4.vin.append(CTxIn(COutPoint(txSuccess1.malfixsha256, 0), b""))
        txSuccess4.vin.append(CTxIn(COutPoint(txSuccess2.malfixsha256, 0), b""))
        txSuccess4.vin.append(CTxIn(COutPoint(coinbase_txs[2].malfixsha256, 0), b""))
        txSuccess4.vout.append(CTxOut(25, script_reissuable))
        txSuccess4.vout.append(CTxOut(75, script_reissuable))
        txSuccess4.vout.append(CTxOut(40, script_nonreissuable))
        txSuccess4.vout.append(CTxOut(60, script_nonreissuable))
        sig_hash, err = SignatureHash(txSuccess1.vout[0].scriptPubKey, txSuccess4, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess4.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess2.vout[0].scriptPubKey, txSuccess4, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess4.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[2].vout[0].scriptPubKey, txSuccess4, 2, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess4.vin[2].scriptSig = CScript([signature])
        txSuccess4.rehash()

        test_transaction_acceptance(node, txSuccess4, accepted=True)

        #TxFailure5 - (UTXO-6)    - split REISSUABLE(75)           (UTXO-10,11)
        #           - (UTXO-7)    - split NON-REISSUABLE(40)       (UTXO-12)
        #           - (UTXO-4)    - split NFT                      (UTXO-13)
        #           - coinbaseTx4
        TxFailure5 = CTransaction()
        TxFailure5.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 1), b""))
        TxFailure5.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 2), b""))
        TxFailure5.vin.append(CTxIn(COutPoint(txSuccess3.malfixsha256, 0), b""))
        TxFailure5.vin.append(CTxIn(COutPoint(coinbase_txs[3].malfixsha256, 0), b""))
        TxFailure5.vout.append(CTxOut(35, script_reissuable))
        TxFailure5.vout.append(CTxOut(40, script_reissuable))
        TxFailure5.vout.append(CTxOut(20, script_nonreissuable))
        TxFailure5.vout.append(CTxOut(20, script_nonreissuable))
        TxFailure5.vout.append(CTxOut(1, script_nft))
        TxFailure5.vout.append(CTxOut(1, script_nft))
        sig_hash, err = SignatureHash(txSuccess4.vout[1].scriptPubKey, TxFailure5, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure5.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess4.vout[2].scriptPubKey, TxFailure5, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure5.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess3.vout[0].scriptPubKey, TxFailure5, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure5.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[3].vout[0].scriptPubKey, TxFailure5, 3, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        TxFailure5.vin[3].scriptSig = CScript([signature])
        TxFailure5.rehash()

        # Splitting an NFT into two outputs triggers bad-txns-nft-output-count (DoS 100),
        # which causes the node to disconnect the peer.
        test_transaction_acceptance(node, TxFailure5, accepted=False, reason=b"bad-txns-nft-output-count", expect_disconnect=True)

        #txSuccess5 - (UTXO-6)    - split REISSUABLE(75)           (UTXO-10,11)
        #           - (UTXO-7)    - split NON-REISSUABLE(40)       (UTXO-12)
        #           - (UTXO-4)    - transfer NFT                      (UTXO-13)
        #           - coinbaseTx4
        txSuccess5 = CTransaction()
        txSuccess5.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 1), b""))
        txSuccess5.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 2), b""))
        txSuccess5.vin.append(CTxIn(COutPoint(txSuccess3.malfixsha256, 0), b""))
        txSuccess5.vin.append(CTxIn(COutPoint(coinbase_txs[3].malfixsha256, 0), b""))
        txSuccess5.vout.append(CTxOut(35, script_reissuable))
        txSuccess5.vout.append(CTxOut(40, script_reissuable))
        txSuccess5.vout.append(CTxOut(20, script_nonreissuable))
        txSuccess5.vout.append(CTxOut(20, script_nonreissuable))
        txSuccess5.vout.append(CTxOut(1, script_nft))
        sig_hash, err = SignatureHash(txSuccess4.vout[1].scriptPubKey, txSuccess5, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess5.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess4.vout[2].scriptPubKey, txSuccess5, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess5.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess3.vout[0].scriptPubKey, txSuccess5, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess5.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[3].vout[0].scriptPubKey, txSuccess5, 3, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess5.vin[3].scriptSig = CScript([signature])
        txSuccess5.rehash()

        test_transaction_acceptance(node, txSuccess5, accepted=True)

        #TxFailure6 - (UTXO-11)   - transfer REISSUABLE(40)        (UTXO-14)
        #           - (UTXO-8)    - burn NON-REISSUABLE(60)        (UTXO-15)*
        #           - (UTXO-13)   - transfer NFT                   (UTXO-16)
        #           - coinbaseTx5 - issue 1000 REISSUABLE1, change (UTXO-17)
        colorId_reissuable1 = colorIdReissuable(coinbase_txs[6].vout[0].scriptPubKey)
        script_reissuable1 = CP2PHK_script(colorId = colorId_reissuable, pubkey = self.pubkeys[0])

        TxFailure6 = CTransaction()
        TxFailure6.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 1), b""))
        TxFailure6.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 3), b""))
        TxFailure6.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 4), b""))
        TxFailure6.vin.append(CTxIn(COutPoint(coinbase_txs[4].malfixsha256, 0), b""))
        TxFailure6.vout.append(CTxOut(40, script_transfer_reissuable))
        TxFailure6.vout.append(CTxOut(30, script_transfer_nonreissuable))
        TxFailure6.vout.append(CTxOut(1, script_transfer_nft))
        TxFailure6.vout.append(CTxOut(1000, script_reissuable1))
        TxFailure6.vout.append(CTxOut(1*COIN, change_script))
        sig_hash, err = SignatureHash(txSuccess5.vout[1].scriptPubKey, TxFailure6, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure6.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess4.vout[3].scriptPubKey, TxFailure6, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure6.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess5.vout[4].scriptPubKey, TxFailure6, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure6.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[4].vout[0].scriptPubKey, TxFailure6, 3, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        TxFailure6.vin[3].scriptSig = CScript([signature])
        TxFailure6.rehash()

        test_transaction_acceptance(node, TxFailure6, accepted=False, reason=b"bad-txns-token-balance")

        #TxSuccess6 - (UTXO-11)   - transfer REISSUABLE(40)        (UTXO-14)
        #           - (UTXO-8)    - burn NON-REISSUABLE(60)        (UTXO-15)*
        #           - (UTXO-13)   - transfer NFT                   (UTXO-16)
        #           - coinbaseTx5 - change
        txSuccess6 = CTransaction()
        txSuccess6.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 1), b""))
        txSuccess6.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 3), b""))
        txSuccess6.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 4), b""))
        txSuccess6.vin.append(CTxIn(COutPoint(coinbase_txs[4].malfixsha256, 0), b""))
        txSuccess6.vout.append(CTxOut(40, script_transfer_reissuable))
        txSuccess6.vout.append(CTxOut(30, script_transfer_nonreissuable))
        txSuccess6.vout.append(CTxOut(1, script_transfer_nft))
        txSuccess6.vout.append(CTxOut(1*COIN, change_script))
        sig_hash, err = SignatureHash(txSuccess5.vout[1].scriptPubKey, txSuccess6, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess6.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess4.vout[3].scriptPubKey, txSuccess6, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess6.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess5.vout[4].scriptPubKey, txSuccess6, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess6.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[4].vout[0].scriptPubKey, txSuccess6, 3, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess6.vin[3].scriptSig = CScript([signature])
        txSuccess6.rehash()

        test_transaction_acceptance(node, txSuccess6, accepted=True)


        #TxSuccess7 - coinbaseTx5 - issue 1000 REISSUABLE1, change (UTXO-17)
        txSuccess7 = CTransaction()
        txSuccess7.vin.append(CTxIn(COutPoint(coinbase_txs[5].malfixsha256, 0), b""))
        txSuccess7.vout.append(CTxOut(1000, script_reissuable1))
        txSuccess7.vout.append(CTxOut(4999999731, change_script))
        sig_hash, err = SignatureHash(coinbase_txs[5].vout[0].scriptPubKey, txSuccess7, 0, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess7.vin[0].scriptSig = CScript([signature])
        txSuccess7.rehash()

        test_transaction_acceptance(node, txSuccess7, accepted=True)

        #TxFailure7 - (UTXO-9,14) - aggregate REISSUABLE(25 + 40) x
        #           - (UTXO-12)   - burn NON-REISSUABLE(20)        *
        TxFailure7 = CTransaction()
        TxFailure7.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 0), b""))
        TxFailure7.vin.append(CTxIn(COutPoint(txSuccess6.malfixsha256, 0), b""))
        TxFailure7.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 2), b""))
        TxFailure7.vout.append(CTxOut(65, script_transfer_reissuable))
        sig_hash, err = SignatureHash(txSuccess4.vout[0].scriptPubKey, TxFailure7, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure7.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess6.vout[0].scriptPubKey, TxFailure7, 1, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure7.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess5.vout[2].scriptPubKey, TxFailure7, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        TxFailure7.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        TxFailure7.rehash()

        test_transaction_acceptance(node, TxFailure7, accepted=False, reason=b'min relay fee not met')

        #txSuccess8 - (UTXO-9,14) - aggregate REISSUABLE(25 + 40) x
        #           - (UTXO-12)   - burn NON-REISSUABLE(20)        *
        #           - coinbase[6]
        txSuccess8 = CTransaction()
        txSuccess8.vin.append(CTxIn(COutPoint(txSuccess4.malfixsha256, 0), b""))
        txSuccess8.vin.append(CTxIn(COutPoint(txSuccess6.malfixsha256, 0), b""))
        txSuccess8.vin.append(CTxIn(COutPoint(txSuccess5.malfixsha256, 2), b""))
        txSuccess8.vin.append(CTxIn(COutPoint(coinbase_txs[6].malfixsha256, 0), b""))
        txSuccess8.vout.append(CTxOut(65, script_transfer_reissuable))
        sig_hash, err = SignatureHash(txSuccess4.vout[0].scriptPubKey, txSuccess8, 0, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess8.vin[0].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(txSuccess6.vout[0].scriptPubKey, txSuccess8, 1, SIGHASH_ALL)
        signature = self.privkeys[1].sign(sig_hash) + b'\x01'
        txSuccess8.vin[1].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[1])])
        sig_hash, err = SignatureHash(txSuccess5.vout[2].scriptPubKey, txSuccess8, 2, SIGHASH_ALL)
        signature = self.privkeys[0].sign(sig_hash) + b'\x01'
        txSuccess8.vin[2].scriptSig = CScript([signature, hex_str_to_bytes(self.pubkeys[0])])
        sig_hash, err = SignatureHash(coinbase_txs[6].vout[0].scriptPubKey, txSuccess8, 3, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess8.vin[3].scriptSig = CScript([signature])
        txSuccess8.rehash()

        test_transaction_acceptance(node, txSuccess8, accepted=True)

        #TxFailure8 - (UTXO-17)   - convert REISSUABLE to NON-REISSUABLE
        TxFailure8 = CTransaction()
        TxFailure8.vin.append(CTxIn(COutPoint(txSuccess7.malfixsha256, 0), b""))
        TxFailure8.vout.append(CTxOut(60, script_transfer_nonreissuable))
        sig_hash, err = SignatureHash(txSuccess7.vout[0].scriptPubKey, TxFailure8, 0, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        TxFailure8.vin[0].scriptSig = CScript([signature])
        TxFailure8.rehash()

        test_transaction_acceptance(node, TxFailure8, accepted=False, reason=b'bad-txns-token-noinput', expect_disconnect=True)

        #TxSuccess9 - (UTXO-17) - issue 10 REISSUABLE1, change
        txSuccess9 = CTransaction()
        txSuccess9.vin.append(CTxIn(COutPoint(txSuccess7.malfixsha256, 1), b""))
        txSuccess9.vout.append(CTxOut(10, script_reissuable1))
        txSuccess9.vout.append(CTxOut(4999999649, change_script))
        sig_hash, err = SignatureHash(txSuccess7.vout[1].scriptPubKey, txSuccess9, 1, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess9.vin[0].scriptSig = CScript([signature])
        #make fee < min relay fee and re sign
        txSuccess9.vout[1].nValue = txSuccess7.vout[1].nValue + 2 - len(txSuccess9.serialize_without_witness())
        sig_hash, err = SignatureHash(txSuccess7.vout[1].scriptPubKey, txSuccess9, 1, SIGHASH_ALL)
        signature = self.coinbase_key.sign(sig_hash) + b'\x01'
        txSuccess9.vin[0].scriptSig = CScript([signature])
        txSuccess9.rehash()

        test_transaction_acceptance(node, txSuccess9, accepted=False, reason=b'min relay fee not met')

        # --- Consensus rule / attack-scenario tests ---
        self.log.info("Starting consensus rule and attack-scenario tests...")

        # Set up a P2PInterface peer for block-level rejection tests.
        self.peer = node.add_p2p_connection(P2PInterface(node.time_to_connect))
        # Generate blocks so the wallet has mature TPC UTXOs for RPC-based tests.
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

        # Reindex regression tests: verify g_issued_colorids is correctly
        # cleared and rebuilt after -reindex-chainstate and -reindex.
        self.test_reindex_chainstate_colorid_set()
        self.test_reindex_colorid_set()

if __name__ == '__main__':
    ColoredCoinTest().main()
