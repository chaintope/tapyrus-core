#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Multi-party PSTT (TIP-174) network test.

Six nodes, one fixed pipeline role each for the whole test (see doc/tapyrus/pstt.md):

  0        Creator       always builds the round's skeleton PSTT
  1, 2, 3  Contributors  candidate colored-coin owners; add + sign their own
                         input(s) when their coin is picked for a round
  4        Signer        always does the final walletprocesspstt
                         completeness-check pass before the fee provider
  5        FeeProvider   fixed for the whole test; sole holder of TPC; funds,
                         signs, finalizes, broadcasts, mines every round

Nodes 0-4 collectively "the peers" start with ten distinct colored coins
between them (five single-owner, three 2-of-2 CP2SH multisig, two 2-of-3 CP2SH
multisig) and zero TPC. Coins move between peers in randomized rounds --
1 to 3 colors per round, contributed by whichever peer(s)/multisig group(s)
currently hold them -- until every color has moved at least once. Every
intermediate step is checked: signing status/signature validity via
decodepstt, and that finalizepstt(extract=true) refuses an incomplete PSTT.
Alongside the round loop: double-spend attempts on already-spent round UTXOs,
one deliberate simulation of every SignPSTTInput validation error, two
deliberate non-canonical-signature rejections (ECDSA high-S, Schnorr
R.y-not-a-quadratic-residue), and a final per-node balance reconciliation.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.address import keyhash_to_p2pkh, scripthash_to_p2sh
from test_framework.script import CScript, OP_CHECKMULTISIG, OP_1, SignatureHash, SIGHASH_ALL, hash160
from test_framework.messages import CTransaction, ser_compact_size, deser_compact_size, hash256
from test_framework.key import CECKey, SECP256K1_ORDER
from test_framework.schnorr import Schnorr, SECP256K1_ORDER as SCHNORR_ORDER, SECP256K1_FIELDSIZE

import base64
import io
import os
import random
import struct


# -----------------------------------------------------------------------
# PSTT wire-format container -- decode/patch/re-encode at the raw KV-map
# level (compact-size-length-prefixed key/value pairs, one map per input/
# output, each self-terminated by a zero-length key), independent of and
# below the RPC layer. Needed for the four PSTTSignResult errors that have
# no RPC-exposed way to set the underlying field (a legitimate Constructor
# can't fabricate a wrong UTXO, a wrong redeem script, a required sighash
# type, or a locktime requirement -- by design), and for splicing tampered
# final scriptSigs into an otherwise-valid PSTT for the negative-signature
# checks. Mirrors rpc_pstt.py's make_bare_pstt_with_unknown_global_field
# helper, generalized into a full read/write round-trip.
# -----------------------------------------------------------------------

PSTT_MAGIC = bytes([0x70, 0x73, 0x74, 0x74, 0xFF])

PSTT_GLOBAL_INPUT_COUNT = 0x04
PSTT_GLOBAL_OUTPUT_COUNT = 0x05

PSTT_IN_UTXO = 0x00
PSTT_IN_SIGHASH_TYPE = 0x03
PSTT_IN_REDEEM_SCRIPT = 0x04
PSTT_IN_REQUIRED_TIME_LOCKTIME = 0x11
PSTT_IN_REQUIRED_HEIGHT_LOCKTIME = 0x12


def _read_kv_map(f):
    """Read one KV map (global, or one input's, or one output's) from a
    BytesIO positioned at its start. Returns a list of (key_bytes,
    value_bytes) tuples; stops at (and consumes) the terminating
    zero-length key."""
    entries = []
    while True:
        keylen = deser_compact_size(f)
        if keylen == 0:
            break
        key = f.read(keylen)
        vallen = deser_compact_size(f)
        val = f.read(vallen)
        entries.append((key, val))
    return entries


def _write_kv_map(entries):
    out = b""
    for key, val in entries:
        out += ser_compact_size(len(key)) + key
        out += ser_compact_size(len(val)) + val
    out += ser_compact_size(0)
    return out


def _find_field(entries, keytype, key_extra=b""):
    key = bytes([keytype]) + key_extra
    for k, v in entries:
        if k == key:
            return v
    return None


def decode_pstt_container(pstt_b64):
    """Returns (global_entries, [input_entries...], [output_entries...])."""
    raw = base64.b64decode(pstt_b64)
    f = io.BytesIO(raw)
    magic = f.read(5)
    assert magic == PSTT_MAGIC, "not a PSTT"
    global_entries = _read_kv_map(f)
    input_count = int.from_bytes(_find_field(global_entries, PSTT_GLOBAL_INPUT_COUNT), 'little')
    output_count = int.from_bytes(_find_field(global_entries, PSTT_GLOBAL_OUTPUT_COUNT), 'little')
    inputs = [_read_kv_map(f) for _ in range(input_count)]
    outputs = [_read_kv_map(f) for _ in range(output_count)]
    assert f.read() == b"", "trailing bytes after PSTT container"
    return global_entries, inputs, outputs


def encode_pstt_container(global_entries, inputs, outputs):
    out = PSTT_MAGIC + _write_kv_map(global_entries)
    for entries in inputs:
        out += _write_kv_map(entries)
    for entries in outputs:
        out += _write_kv_map(entries)
    return base64.b64encode(out).decode()


def patch_input_field(entries, keytype, value, key_extra=b""):
    """Insert or overwrite a field (identified by its single-byte keytype
    plus optional extra key bytes, e.g. a pubkey for PSTT_IN_PARTIAL_SIG) in
    an input's KV-entry list, in place."""
    key = bytes([keytype]) + key_extra
    for i, (k, v) in enumerate(entries):
        if k == key:
            entries[i] = (key, value)
            return
    entries.append((key, value))


# -----------------------------------------------------------------------
# Negative-signature construction. Confirmed against the real crypto:
# CPubKey::Verify (src/pubkey.cpp) normalizes S before checking, so a
# high-S ECDSA signature is cryptographically valid but policy-invalid
# (non-canonical DER, standardness-only rejection). Tapyrus's custom
# Schnorr (secp256k1_schnorr_verify, src/secp256k1/src/modules/schnorr/
# main_impl.h) requires R.y be a quadratic residue; secp256k1_schnorr_sign
# always negates the nonce to guarantee this, so no legitimately-produced
# signature is ever non-canonical -- constructing one needs a hand-rolled
# variant of test_framework/schnorr.py's Schnorr.sign() with the
# canonicalization condition inverted.
# -----------------------------------------------------------------------

def der_negate_s(der_sig):
    """Given a DER-encoded ECDSA signature (no trailing sighash byte),
    return the 'other root' -- same r, s replaced by SECP256K1_ORDER - s.
    Mathematically still a valid signature for the same message/key (both
    roots satisfy the ECDSA verify equation), but non-canonical/high-S,
    which CheckECDSASignatureEncoding's IsLowDERSignature must reject."""
    assert der_sig[0] == 0x30
    r_len = der_sig[3]
    r_bytes = der_sig[4:4 + r_len]
    s_marker_off = 4 + r_len
    assert der_sig[s_marker_off] == 0x02
    s_len = der_sig[s_marker_off + 1]
    s_bytes = der_sig[s_marker_off + 2: s_marker_off + 2 + s_len]
    s_value = int.from_bytes(s_bytes, 'big')
    new_s = SECP256K1_ORDER - s_value
    new_s_bytes = new_s.to_bytes(33, 'big')
    while len(new_s_bytes) > 1 and new_s_bytes[0] == 0 and new_s_bytes[1] < 0x80:
        new_s_bytes = new_s_bytes[1:]
    new_total = 2 + r_len + 2 + len(new_s_bytes)
    return b'\x30' + bytes([new_total]) + b'\x02' + bytes([r_len]) + r_bytes + b'\x02' + bytes([len(new_s_bytes)]) + new_s_bytes


class NonCanonicalSchnorr(Schnorr):
    """Sign variant that inverts schnorr.py's own canonicalization check:
    negates the nonce when R.y IS a quadratic residue instead of when it
    ISN'T, so the resulting signature's R.y is deliberately the non-residue
    -- the exact condition secp256k1_schnorr_verify's
    'Reject if ... R.y is not a quadratic residue' check exists to catch."""

    def sign_noncanonical(self, msg32):
        import hashlib
        import ctypes
        from test_framework.schnorr import group, ssl, POINT_CONVERSION_UNCOMPRESSED, POINT_CONVERSION_COMPRESSED

        assert len(msg32) == 32
        k = self._nonce_function_rfc6979(msg32, algo16=b"SCHNORR + SHA256")

        ctx = self.ptr_for_this_thread()
        R = ssl.EC_POINT_new(group)
        pubkey = ssl.EC_POINT_new(group)
        kbn = ssl.BN_bin2bn(k.to_bytes(32, 'big'), 32, None)
        privbn = ssl.BN_bin2bn(self.privkeybytes, 32, None)
        assert ssl.EC_POINT_mul(group, R, kbn, None, None, ctx)
        assert ssl.EC_POINT_mul(group, pubkey, privbn, None, None, ctx)
        Rbuf = ctypes.create_string_buffer(65)
        assert 65 == ssl.EC_POINT_point2oct(group, R, POINT_CONVERSION_UNCOMPRESSED, Rbuf, 65, ctx)
        pubkeybuf = ctypes.create_string_buffer(33)
        assert 33 == ssl.EC_POINT_point2oct(group, pubkey, POINT_CONVERSION_COMPRESSED, pubkeybuf, 33, ctx)
        ssl.BN_free(kbn)
        ssl.BN_free(privbn)
        ssl.EC_POINT_free(R)
        ssl.EC_POINT_free(pubkey)

        Ry = int.from_bytes(Rbuf[33:65], 'big')
        # Inverted vs. the real algorithm: force the non-residue instead of
        # correcting away from it.
        if self._jacobi(Ry, SECP256K1_FIELDSIZE) == 1:
            k = SCHNORR_ORDER - k

        rbytes = Rbuf[1:33]
        e = int.from_bytes(hashlib.sha256(rbytes + pubkeybuf + msg32).digest(), 'big')
        privkey = int.from_bytes(self.privkeybytes, 'big')
        s = (k + e * privkey) % SCHNORR_ORDER
        return rbytes + s.to_bytes(32, 'big')


def wif_encode(secret32, prefix=239, compressed=True):
    """WIF-encode a raw 32-byte secret for importprivkey. prefix=239 is
    Base58Prefix(SECRET_KEY) for Tapyrus's non-main networks
    (src/chainparams.cpp) -- the same convention test_framework/address.py
    already uses (main=False) for every other address type in this suite."""
    payload = bytes([prefix]) + secret32 + (b'\x01' if compressed else b'')
    checksum = hash256(payload)[:4]
    data = payload + checksum
    n = int.from_bytes(data, 'big')
    chars = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
    result = ''
    while n > 0:
        n, r = divmod(n, 58)
        result = chars[r] + result
    n_leading_zeros = len(data) - len(data.lstrip(b'\x00'))
    return '1' * n_leading_zeros + result


def new_keypair():
    """Returns (secret32, CECKey) for a fresh random secp256k1 keypair."""
    secret = os.urandom(32)
    k = CECKey()
    k.set_secretbytes(secret)
    k.set_compressed(True)
    return secret, k


class PSTTNetworkTest(BitcoinTestFramework):

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 6

    def run_test(self):
        self.rng = random.Random(1174)  # deterministic, reproducible runs

        self.CREATOR = 0
        self.CONTRIBUTORS = [1, 2, 3]
        self.SIGNER = 4
        self.FEEPROVIDER = 5
        self.PEERS = [0, 1, 2, 3, 4]

        self.error_coverage = set()

        # Only the fee provider ever mines -- it's the sole TPC source for
        # the whole test (see doc/tapyrus/pstt.md's Fee Provider workflow;
        # "they all have no TPC" for every other peer).
        self.nodes[self.FEEPROVIDER].generate(30, self.signblockprivkey_wif)
        self.sync_all()

        self.issue_coins()
        self.split_fee_utxos()

        self.run_rounds()
        self.run_error_simulations()
        self.run_negative_signature_simulations()
        self.final_reconciliation()

        expected_errors = {
            'MISSING_UTXO', 'UTXO_TXID_MISMATCH', 'PREV_OUT_INDEX_OOB',
            'REDEEM_SCRIPT_HASH_MISMATCH', 'SIGHASH_CONFLICT',
            'SIGHASH_SINGLE_OOB', 'LOCKTIME_INVALID', 'SCHEME_CONFLICT',
        }
        assert_equal(self.error_coverage, expected_errors)
        self.log.info("All 8 PSTTSignResult errors simulated: %s", sorted(self.error_coverage))

    # -------------------------------------------------------------------
    # Coin issuance
    # -------------------------------------------------------------------

    # Five single-owner coins (one per peer) + three 2-of-2 CP2SH multisig
    # coins + two 2-of-3 CP2SH multisig coins, spanning every peer across
    # several different group compositions.
    COIN_TOPOLOGY = [
        {'kind': 'solo', 'owners': [0]},
        {'kind': 'solo', 'owners': [1]},
        {'kind': 'solo', 'owners': [2]},
        {'kind': 'solo', 'owners': [3]},
        {'kind': 'solo', 'owners': [4]},
        {'kind': 'multi', 'owners': [0, 1]},
        {'kind': 'multi', 'owners': [2, 3]},
        {'kind': 'multi', 'owners': [4, 0]},
        {'kind': 'multi', 'owners': [1, 2, 4]},
        {'kind': 'multi', 'owners': [0, 3, 4]},
    ]

    def _pick_any_tpc_utxo(self):
        fp = self.nodes[self.FEEPROVIDER]
        for u in fp.listunspent():
            if u['amount'] > 0:
                return u
        raise AssertionError("fee provider has no spendable TPC left")

    def issue_coins(self):
        """Issues the 10-coin topology above, plus two extra dedicated
        single-owner coins (held by Python-generated keys, never touched by
        the round loop) used only by the negative-signature checks."""
        fp = self.nodes[self.FEEPROVIDER]
        # color_hex -> {'owner': int|[int,...], 'kind', 'amount', 'moved', 'redeem_script'}
        self.ledger = {}
        # color_hex -> {peer_idx: (secret32, CECKey)}, only for 'multi' coins
        self.multisig_keys = {}

        for idx, spec in enumerate(self.COIN_TOPOLOGY):
            amount = 100 + 10 * idx
            fee_utxo = self._pick_any_tpc_utxo()
            issued = fp.issuetoken(2, amount, fee_utxo['txid'], fee_utxo['vout'])
            color_hex = issued['color']
            fp.generate(1, self.signblockprivkey_wif)
            self.sync_all()

            if spec['kind'] == 'solo':
                owner = spec['owners'][0]
                dest_addr = self.nodes[owner].getnewaddress("coin%d" % idx, color_hex)
                redeem_script = None
            else:
                owners = spec['owners']
                keys = {}
                pubkeys_hex = []
                for peer in owners:
                    secret, k = new_keypair()
                    keys[peer] = (secret, k)
                    pubkeys_hex.append(k.get_pubkey().hex())
                self.multisig_keys[color_hex] = keys
                redeem_script_hex = None
                for peer in owners:
                    self.nodes[peer].importprivkey(wif_encode(keys[peer][0]), "", False)
                    reg = self.nodes[peer].addmultisigaddress(len(owners), pubkeys_hex, "")
                    if redeem_script_hex is None:
                        redeem_script_hex = reg['redeemScript']
                redeem_script = bytes.fromhex(redeem_script_hex)
                redeem_hash160 = hash160(redeem_script)
                dest_addr = scripthash_to_p2sh(redeem_hash160, main=False, color=bytes.fromhex(color_hex))
                # addmultisigaddress alone only registers the CScript
                # (mapScripts); IsMine's TX_MULTISIG case requires owning
                # ALL k keys to count a P2SH-multisig output as spendable
                # (deliberately, to prevent spend-out-from-under-you), and
                # each cosigner here holds only one -- so without also
                # marking the colored destination watch-only, no cosigner's
                # wallet would recognize this payment as involving it at
                # all (not even watch-only), and FillPSTT's Updater
                # (mapWallet-based) could never attach its UTXO.
                for peer in owners:
                    self.nodes[peer].importaddress(dest_addr, "", False)

            txid = fp.sendtoaddress(dest_addr, amount)
            fp.generate(1, self.signblockprivkey_wif)
            self.sync_all()
            vout = self._find_vout(txid, dest_addr)

            self.ledger[color_hex] = {
                'owner': spec['owners'][0] if spec['kind'] == 'solo' else list(spec['owners']),
                'kind': spec['kind'],
                'amount': amount,
                'moved': False,
                'redeem_script': redeem_script,
                'txid': txid,
                'vout': vout,
            }

        self.log.info("Issued %d colored coins: %s", len(self.ledger), list(self.ledger.keys()))

        # One extra dedicated coin for the negative-signature check, in
        # whichever scheme this run was invoked with (--scheme, default
        # ECDSA) -- Python-held key only, never imported into any node's
        # wallet, never touched by the round loop.
        scheme = self.options.scheme
        secret, k = new_keypair()
        fee_utxo = self._pick_any_tpc_utxo()
        issued = fp.issuetoken(2, 500, fee_utxo['txid'], fee_utxo['vout'])
        color_hex = issued['color']
        fp.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        dest_addr = keyhash_to_p2pkh(hash160(k.get_pubkey()), main=False, color=bytes.fromhex(color_hex))
        txid = fp.sendtoaddress(dest_addr, 500)
        fp.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        vout = self._find_vout(txid, dest_addr)
        self.sig_test_coin = {
            'secret': secret, 'key': k, 'color': color_hex,
            'txid': txid, 'vout': vout, 'amount': 500, 'address': dest_addr,
        }
        self.log.info("Issued 1 dedicated negative-signature test coin (%s scheme)", scheme)

    def _find_vout(self, txid, address):
        decoded = self.nodes[self.FEEPROVIDER].decoderawtransaction(
            self.nodes[self.FEEPROVIDER].getrawtransaction(txid))
        for out in decoded['vout']:
            addrs = out.get('scriptPubKey', {}).get('addresses', [])
            if address in addrs:
                return out['n']
        raise AssertionError("output paying %s not found in %s" % (address, txid))

    def split_fee_utxos(self):
        """Splits one of the fee provider's remaining coinbase UTXOs into
        many small fixed-value TPC UTXOs so every round's walletfundpsttfee
        call is a simple noninteractive single-UTXO funding, per rule that
        noninteractive mode has no change output."""
        fp = self.nodes[self.FEEPROVIDER]
        n = 30
        outputs = {}
        for i in range(n):
            outputs[fp.getnewaddress("fee%d" % i)] = 1
        raw = fp.createrawtransaction([], outputs)
        funded = fp.fundrawtransaction(raw)['hex']
        signed = fp.signrawtransactionwithwallet(funded)['hex']
        fp.sendrawtransaction(signed)
        fp.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        self.fee_utxos = [u for u in fp.listunspent() if u['amount'] == 1]
        self.log.info("Split %d fee UTXOs for round funding", len(self.fee_utxos))

    def _next_fee_utxo(self):
        return self.fee_utxos.pop()

    # -------------------------------------------------------------------
    # Main round loop
    # -------------------------------------------------------------------

    def _select_round_colors(self):
        unmoved = [c for c, e in self.ledger.items() if not e['moved']]
        moved = [c for c, e in self.ledger.items() if e['moved']]
        n = self.rng.choice([1, 2, 3])
        pool = unmoved if unmoved else moved
        n = min(n, len(pool))
        chosen = self.rng.sample(pool, n)
        # top up with already-moved colors if the unmoved pool ran short,
        # so round size still varies randomly across 1-3 even near the end
        if len(chosen) < min(3, len(unmoved) + len(moved)) and moved:
            extra_pool = [c for c in moved if c not in chosen]
            want = self.rng.choice([1, 2, 3]) - len(chosen)
            if want > 0 and extra_pool:
                chosen += self.rng.sample(extra_pool, min(want, len(extra_pool)))
        return chosen

    def _pick_recipient(self, exclude):
        choices = [p for p in self.PEERS if p not in exclude]
        return self.rng.choice(choices)

    def _assert_not_finalizable(self, pstt):
        assert_raises_rpc_error(None, None, self.nodes[self.CREATOR].finalizepstt, pstt, True)

    def run_rounds(self):
        self.double_spend_hexes = []  # held competing raw txs, submitted post-confirmation
        round_num = 0
        while any(not e['moved'] for e in self.ledger.values()):
            round_num += 1
            colors = self._select_round_colors()
            self.log.info("Round %d: moving colors %s", round_num, colors)

            recipients = {}
            for color in colors:
                owners = self.ledger[color]['owner']
                exclude = set(owners) if isinstance(owners, list) else {owners}
                recipients[color] = self._pick_recipient(exclude)

            outputs = {}
            for color in colors:
                addr = self.nodes[recipients[color]].getnewaddress("recv", color)
                outputs[addr] = self.ledger[color]['amount']
                recipients[color] = (recipients[color], addr)

            pstt = self.nodes[self.CREATOR].createpstt([], outputs, 0, True, False)

            for color in colors:
                pstt = self._contribute_color(pstt, color, round_num)
                self._assert_not_finalizable(pstt)

            processed = self.nodes[self.SIGNER].walletprocesspstt(
                pstt, True, "ALL", False, self.options.scheme)
            pstt = processed['pstt']
            self._assert_not_finalizable(pstt)

            fee_utxo = self._next_fee_utxo()
            funded = self.nodes[self.FEEPROVIDER].walletfundpsttfee(
                pstt, "noninteractive",
                {"previous_txid": fee_utxo['txid'], "output_index": fee_utxo['vout']})
            self._assert_not_finalizable(funded)

            fee_signed = self.nodes[self.FEEPROVIDER].walletsignpstt(funded, "ALL", self.options.scheme)
            assert_equal(fee_signed['complete'], True)
            pstt = fee_signed['pstt']

            final = self.nodes[self.FEEPROVIDER].finalizepstt(pstt, True)
            assert_equal(final['complete'], True)
            self.nodes[self.FEEPROVIDER].sendrawtransaction(final['hex'], True)
            self.nodes[self.FEEPROVIDER].generate(1, self.signblockprivkey_wif)
            self.sync_all()

            self._submit_pending_double_spends()

            confirmed_txid = self.nodes[self.FEEPROVIDER].decoderawtransaction(final['hex'])['txid']
            for color in colors:
                new_owner, addr = recipients[color]
                vout = self._find_vout(confirmed_txid, addr)
                self.ledger[color].update({
                    'owner': new_owner, 'kind': 'solo', 'moved': True,
                    'redeem_script': None, 'txid': confirmed_txid, 'vout': vout,
                })

            self._verify_balances_after_round(colors)
            self.log.info("Round %d confirmed: %s", round_num, colors)

        self.log.info("All 10 colors moved at least once after %d rounds", round_num)

    def _contribute_color(self, pstt, color, round_num):
        entry = self.ledger[color]
        txid, vout = entry['txid'], entry['vout']
        owners = entry['owner']

        if entry['kind'] == 'solo':
            owner = owners
            owning_node = self.nodes[owner]

            # Interleave a double-spend attempt on this exact UTXO for
            # roughly every third solo contribution ("added to the mix").
            if round_num % 3 == 0:
                self._prepare_double_spend(owner, txid, vout, entry['amount'], color)

            pstt = owning_node.addinputtopstt(pstt, txid, vout)
            pstt = owning_node.walletupdatepstt(pstt)['pstt']
            signed = owning_node.walletsignpstt(pstt, "ALL|ANYONECANPAY", self.options.scheme)
            assert_equal(signed['complete'], True)
            pstt = signed['pstt']

            decoded = self.nodes[self.CREATOR].decodepstt(pstt)
            in_idx = self._input_index(decoded, txid, vout)
            assert 'final_scriptSig' in decoded['inputs'][in_idx]
            return pstt

        # multisig: one member adds the input and attaches its UTXO (an
        # Updater pass, needed only once -- PSTT_IN_UTXO is a wire field
        # that persists in the base64 handed to each next cosigner), then
        # every member in turn co-signs (k-of-k) until the input is
        # complete. Deliberately NOT re-running walletupdatepstt per
        # cosigner: it has no sigscheme param and always probes with the
        # ECDSA default internally, which would spuriously conflict with an
        # already-attached SCHNORR-scheme partial signature from an earlier
        # cosigner (rule 31) before this cosigner ever got to sign.
        first = owners[0]
        pstt = self.nodes[first].addinputtopstt(pstt, txid, vout)
        pstt = self.nodes[first].walletupdatepstt(pstt)['pstt']
        for i, peer in enumerate(owners):
            signed = self.nodes[peer].walletsignpstt(pstt, "ALL|ANYONECANPAY", self.options.scheme)
            pstt = signed['pstt']
            decoded = self.nodes[self.CREATOR].decodepstt(pstt)
            in_idx = self._input_index(decoded, txid, vout)
            input_view = decoded['inputs'][in_idx]
            if i < len(owners) - 1:
                assert_equal(signed['complete'], False)
                assert 'final_scriptSig' not in input_view
                assert_equal(len(input_view.get('partial_signatures', {})), i + 1)
            else:
                assert_equal(signed['complete'], True)
                assert 'final_scriptSig' in input_view
        return pstt

    @staticmethod
    def _input_index(decoded, txid, vout):
        for i, inp in enumerate(decoded['inputs']):
            if inp['previous_txid'] == txid and inp['output_index'] == vout:
                return i
        raise AssertionError("input %s:%d not found in decoded PSTT" % (txid, vout))

    def _verify_balances_after_round(self, colors):
        for color in colors:
            entry = self.ledger[color]
            owner = entry['owner']
            for peer in self.PEERS:
                expected = entry['amount'] if peer == owner else 0
                actual = self.nodes[peer].getbalance(False, color)
                assert_equal(actual, expected)
        for peer in self.PEERS:
            assert_equal(self.nodes[peer].getbalance(), 0)
        assert self.nodes[self.FEEPROVIDER].getbalance() > 0

    # -------------------------------------------------------------------
    # Double-spend injection
    # -------------------------------------------------------------------

    def _prepare_double_spend(self, owner, txid, vout, amount, color):
        """Builds (but does not yet submit) a transaction from the current
        owner spending the same UTXO the legitimate round is about to
        consume, to an unrelated throwaway address."""
        node = self.nodes[owner]
        throwaway = node.getnewaddress("doublespend", color)
        raw = node.createrawtransaction([{"txid": txid, "vout": vout}], {throwaway: amount})
        signed = node.signrawtransactionwithwallet(raw)
        assert_equal(signed['complete'], True)
        self.double_spend_hexes.append((owner, signed['hex'], txid, vout))

    def _submit_pending_double_spends(self):
        for owner, hex_tx, txid, vout in self.double_spend_hexes:
            assert_raises_rpc_error(None, None, self.nodes[owner].sendrawtransaction, hex_tx, True)
        if self.double_spend_hexes:
            self.log.info("Rejected %d double-spend attempt(s) on already-spent UTXOs",
                           len(self.double_spend_hexes))
        self.double_spend_hexes = []

    # -------------------------------------------------------------------
    # PSTTSignResult error simulations -- each triggered once, via real
    # RPC calls against the running network. The four with no RPC-exposed
    # way to set the underlying field go through the wire-format patcher
    # instead (see module docstring/header comment above).
    # -------------------------------------------------------------------

    def run_error_simulations(self):
        colors = list(self.ledger.keys())
        color_a, color_b = colors[0], colors[1]
        entry_a, entry_b = self.ledger[color_a], self.ledger[color_b]
        owner_a, owner_b = entry_a['owner'], entry_b['owner']
        node_a, node_b = self.nodes[owner_a], self.nodes[owner_b]
        addr_a = node_a.getnewaddress("errsim", color_a)
        addr_b = node_b.getnewaddress("errsim", color_b)

        self._sim_missing_utxo(entry_a, owner_a, addr_a)
        self._sim_prev_out_index_oob(entry_a, owner_a, addr_a)
        self._sim_sighash_single_oob(entry_a, entry_b, addr_a)
        self._sim_utxo_txid_mismatch(entry_a, entry_b, owner_a, addr_a)
        self._sim_sighash_conflict(entry_a, owner_a, addr_a)

        multi_color, multi_owners, multi_txid, multi_vout, multi_redeem = self._issue_standalone_multisig_coin()
        self._sim_scheme_conflict(multi_color, multi_owners, multi_txid, multi_vout, addr_a)
        self._sim_redeem_script_hash_mismatch(multi_color, multi_owners, multi_txid, multi_vout, addr_a)
        self._sim_locktime_invalid(entry_a, entry_b, owner_a, owner_b, addr_a)

        self.log.info("Simulated error coverage so far: %s", sorted(self.error_coverage))

    def _sim_missing_utxo(self, entry, owner, addr):
        node = self.nodes[owner]
        pstt = node.createpstt(
            [{"previous_txid": entry['txid'], "output_index": entry['vout']}],
            {addr: entry['amount']}, 0, True, False)
        # No walletupdatepstt -- the input has no PSTT_IN_UTXO yet.
        signed = node.walletsignpstt(pstt, "ALL", self.options.scheme)
        assert_equal(signed['complete'], False)
        decoded = node.decodepstt(signed['pstt'])
        assert 'utxo' not in decoded['inputs'][0]
        assert 'final_scriptSig' not in decoded['inputs'][0]
        self.error_coverage.add('MISSING_UTXO')

    def _sim_prev_out_index_oob(self, entry, owner, addr):
        node = self.nodes[owner]
        pstt = node.createpstt([], {addr: entry['amount']}, 0, True, False)
        pstt = node.addinputtopstt(pstt, entry['txid'], 99)
        assert_raises_rpc_error(None, "prevout index out of range", node.walletupdatepstt, pstt)
        self.error_coverage.add('PREV_OUT_INDEX_OOB')

    def _sim_sighash_single_oob(self, entry_a, entry_b, addr_a):
        # 2 inputs, 1 output -- signing input index 1 with SIGHASH_SINGLE
        # has no corresponding output.
        node_b = self.nodes[entry_b['owner']]
        pstt = self.nodes[self.CREATOR].createpstt([], {addr_a: entry_a['amount']}, 0, True, False)
        pstt = self.nodes[self.CREATOR].addinputtopstt(pstt, entry_a['txid'], entry_a['vout'])
        pstt = self.nodes[self.CREATOR].addinputtopstt(pstt, entry_b['txid'], entry_b['vout'])
        pstt = node_b.walletupdatepstt(pstt)['pstt']
        assert_raises_rpc_error(None, "SIGHASH_SINGLE", node_b.walletsignpstt, pstt, "SINGLE", self.options.scheme)
        self.error_coverage.add('SIGHASH_SINGLE_OOB')

    def _sim_utxo_txid_mismatch(self, entry_a, entry_b, owner_a, addr_a):
        node = self.nodes[owner_a]
        pstt = node.createpstt(
            [{"previous_txid": entry_a['txid'], "output_index": entry_a['vout']}],
            {addr_a: entry_a['amount']}, 0, True, False)
        pstt = node.walletupdatepstt(pstt)['pstt']
        wrong_tx_bytes = bytes.fromhex(node.getrawtransaction(entry_b['txid']))
        privkey = node.dumpprivkey(node.decodepstt(pstt)['inputs'][0]['utxo']['vout'][entry_a['vout']]['scriptPubKey']['addresses'][0])

        g, ins, outs = decode_pstt_container(pstt)
        patch_input_field(ins[0], PSTT_IN_UTXO, wrong_tx_bytes)
        tampered = encode_pstt_container(g, ins, outs)
        assert_raises_rpc_error(None, "PSTT_IN_UTXO txid does not match", self.nodes[self.CREATOR].signpsttwithkey, tampered, [privkey])
        self.error_coverage.add('UTXO_TXID_MISMATCH')

    def _sim_sighash_conflict(self, entry, owner, addr):
        node = self.nodes[owner]
        pstt = node.createpstt(
            [{"previous_txid": entry['txid'], "output_index": entry['vout']}],
            {addr: entry['amount']}, 0, True, False)
        pstt = node.walletupdatepstt(pstt)['pstt']
        addr_owning = node.decodepstt(pstt)['inputs'][0]['utxo']['vout'][entry['vout']]['scriptPubKey']['addresses'][0]
        privkey = node.dumpprivkey(addr_owning)

        g, ins, outs = decode_pstt_container(pstt)
        patch_input_field(ins[0], PSTT_IN_SIGHASH_TYPE, struct.pack("<i", SIGHASH_ALL))
        tampered = encode_pstt_container(g, ins, outs)
        assert_raises_rpc_error(None, "sighash type conflicts", self.nodes[self.CREATOR].signpsttwithkey, tampered, [privkey], "NONE")
        self.error_coverage.add('SIGHASH_CONFLICT')

    def _issue_standalone_multisig_coin(self):
        """A reusable 2-of-2 multisig coin, never actually spent (every
        simulation that touches it deliberately fails before completion),
        for the SCHEME_CONFLICT and REDEEM_SCRIPT_HASH_MISMATCH checks."""
        owners = [0, 1]
        keys = {}
        pubkeys_hex = []
        for peer in owners:
            secret, k = new_keypair()
            keys[peer] = (secret, k)
            pubkeys_hex.append(k.get_pubkey().hex())
        redeem_script_hex = None
        for peer in owners:
            self.nodes[peer].importprivkey(wif_encode(keys[peer][0]), "", False)
            reg = self.nodes[peer].addmultisigaddress(len(owners), pubkeys_hex, "")
            if redeem_script_hex is None:
                redeem_script_hex = reg['redeemScript']
        redeem_script = bytes.fromhex(redeem_script_hex)
        fp = self.nodes[self.FEEPROVIDER]
        fee_utxo = self._pick_any_tpc_utxo()
        issued = fp.issuetoken(2, 250, fee_utxo['txid'], fee_utxo['vout'])
        color_hex = issued['color']
        fp.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        dest_addr = scripthash_to_p2sh(hash160(redeem_script), main=False, color=bytes.fromhex(color_hex))
        for peer in owners:
            self.nodes[peer].importaddress(dest_addr, "", False)
        txid = fp.sendtoaddress(dest_addr, 250)
        fp.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        vout = self._find_vout(txid, dest_addr)
        self.multisig_error_sim_keys = keys
        return color_hex, owners, txid, vout, redeem_script

    def _sim_scheme_conflict(self, color, owners, txid, vout, dummy_addr):
        node0, node1 = self.nodes[owners[0]], self.nodes[owners[1]]
        pstt = node0.createpstt([], {dummy_addr: 1}, 0, True, False)
        pstt = node0.addinputtopstt(pstt, txid, vout)
        pstt = node0.walletupdatepstt(pstt)['pstt']
        signed0 = node0.walletsignpstt(pstt, "ALL|ANYONECANPAY", "ECDSA")
        assert_equal(signed0['complete'], False)
        pstt = signed0['pstt']
        pstt = node1.walletupdatepstt(pstt)['pstt']
        assert_raises_rpc_error(None, "scheme conflict", node1.walletsignpstt, pstt, "ALL|ANYONECANPAY", "SCHNORR")
        self.error_coverage.add('SCHEME_CONFLICT')

    def _sim_redeem_script_hash_mismatch(self, color, owners, txid, vout, dummy_addr):
        node0 = self.nodes[owners[0]]
        pstt = node0.createpstt([], {dummy_addr: 1}, 0, True, False)
        pstt = node0.addinputtopstt(pstt, txid, vout)
        pstt = node0.walletupdatepstt(pstt)['pstt']
        secret, _ = self.multisig_error_sim_keys[owners[0]]
        privkey = wif_encode(secret)

        wrong_redeem = bytes(CScript([OP_1, OP_CHECKMULTISIG]))  # trivially wrong, different hash
        g, ins, outs = decode_pstt_container(pstt)
        patch_input_field(ins[0], PSTT_IN_REDEEM_SCRIPT, bytes(wrong_redeem))
        tampered = encode_pstt_container(g, ins, outs)
        assert_raises_rpc_error(None, "redeem script does not hash", self.nodes[self.CREATOR].signpsttwithkey, tampered, [privkey])
        self.error_coverage.add('REDEEM_SCRIPT_HASH_MISMATCH')

    def _sim_locktime_invalid(self, entry_a, entry_b, owner_a, owner_b, addr_a):
        node_a = self.nodes[owner_a]
        pstt = self.nodes[self.CREATOR].createpstt([], {addr_a: entry_a['amount']}, 0, True, False)
        pstt = self.nodes[self.CREATOR].addinputtopstt(pstt, entry_a['txid'], entry_a['vout'])
        pstt = self.nodes[self.CREATOR].addinputtopstt(pstt, entry_b['txid'], entry_b['vout'])
        pstt = node_a.walletupdatepstt(pstt)['pstt']
        addr_owning = node_a.decodepstt(pstt)['inputs'][0]['utxo']['vout'][entry_a['vout']]['scriptPubKey']['addresses'][0]
        privkey = node_a.dumpprivkey(addr_owning)

        g, ins, outs = decode_pstt_container(pstt)
        patch_input_field(ins[0], PSTT_IN_REQUIRED_HEIGHT_LOCKTIME, struct.pack("<I", 100))
        patch_input_field(ins[1], PSTT_IN_REQUIRED_TIME_LOCKTIME, struct.pack("<I", 1700000000))
        tampered = encode_pstt_container(g, ins, outs)
        assert_raises_rpc_error(None, "no locktime is acceptable", self.nodes[self.CREATOR].signpsttwithkey, tampered, [privkey])
        self.error_coverage.add('LOCKTIME_INVALID')

    # -------------------------------------------------------------------
    # Negative-signature simulations. Built as raw transactions (not
    # through the PSTT layer) since this is fundamentally a script/
    # consensus-level check, mirroring feature_coloredcoin.py's own
    # established pattern for hand-signing a colored spend with
    # test_framework.script.SignatureHash + test_framework.key.CECKey /
    # test_framework.schnorr.Schnorr.
    # -------------------------------------------------------------------

    def run_negative_signature_simulations(self):
        self._sim_negative_signature(self.options.scheme)

    def _sim_negative_signature(self, scheme):
        coin = self.sig_test_coin
        fp = self.nodes[self.FEEPROVIDER]
        prev_script_hex = fp.getrawtransaction(coin['txid'], 1)['vout'][coin['vout']]['scriptPubKey']['hex']
        prev_script = CScript(bytes.fromhex(prev_script_hex))

        dest_addr = self.nodes[self.CREATOR].getnewaddress("negsigtest", coin['color'])
        raw_hex = fp.createrawtransaction([{"txid": coin['txid'], "vout": coin['vout']}], {dest_addr: coin['amount']})
        tx = CTransaction()
        tx.deserialize(io.BytesIO(bytes.fromhex(raw_hex)))

        sig_hash, err = SignatureHash(prev_script, tx, 0, SIGHASH_ALL)
        assert err is None, err
        pubkey_bytes = coin['key'].get_pubkey()

        if scheme == 'ECDSA':
            good_der = coin['key'].sign(sig_hash, low_s=True)
            bad_der = der_negate_s(good_der)
            bad_sig = bad_der + bytes([SIGHASH_ALL])
        else:
            ncs = NonCanonicalSchnorr()
            ncs.set_secretbytes(coin['secret'])
            bad_sig64 = ncs.sign_noncanonical(sig_hash)
            bad_sig = bad_sig64 + bytes([SIGHASH_ALL])

        tx.vin[0].scriptSig = CScript([bad_sig, pubkey_bytes])
        tx.rehash()
        raw_bad = tx.serialize().hex()

        assert_raises_rpc_error(None, None, fp.sendrawtransaction, raw_bad, True)
        self.log.info("Negative %s signature (%s) correctly rejected", scheme,
                       "high-S" if scheme == 'ECDSA' else "R.y not a quadratic residue")

    # -------------------------------------------------------------------
    # Final reconciliation
    # -------------------------------------------------------------------

    def final_reconciliation(self):
        for color, entry in self.ledger.items():
            owner = entry['owner']
            for peer in self.PEERS:
                expected = entry['amount'] if peer == owner else 0
                actual = self.nodes[peer].getbalance(False, color)
                assert_equal(actual, expected)
        for peer in self.PEERS:
            assert_equal(self.nodes[peer].getbalance(), 0)
        assert self.nodes[self.FEEPROVIDER].getbalance() > 0
        self.log.info("Final reconciliation OK: every color's balance matches the ledger "
                       "on all 6 nodes; TPC is held only by the fee provider")


if __name__ == '__main__':
    PSTTNetworkTest().main()
