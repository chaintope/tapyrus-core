#!/usr/bin/env python3
# Copyright (c) 2016-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the bumpfee RPC.

Verifies that the bumpfee RPC creates replacement transactions successfully when
its preconditions are met, and returns appropriate errors in other cases.

This module consists of around a dozen individual test cases implemented in the
top-level functions named as test_<test_case_description>. The test functions
can be disabled or reordered if needed for debugging. If new test cases are
added in the future, they should try to follow the same convention and not
make assumptions about execution order.
"""

from decimal import Decimal

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import BIP125_SEQUENCE_NUMBER, CTransaction
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_raises_rpc_error, bytes_to_hex_str, connect_nodes_bi, hex_str_to_bytes, sync_mempools

import io

WALLET_PASSPHRASE = "test"
WALLET_PASSPHRASE_TIMEOUT = 3600
SCHEME = None


class BumpFeeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [["-walletrbf={}".format(i)]
                           for i in range(self.num_nodes)]

    def run_test(self):
        # Encrypt wallet for test_locked_wallet_fails test
        SCHEME = self.options.scheme
        self.nodes[1].node_encrypt_wallet(WALLET_PASSPHRASE)
        self.start_node(1)
        self.nodes[1].walletpassphrase(WALLET_PASSPHRASE, WALLET_PASSPHRASE_TIMEOUT)

        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_all()

        peer_node, rbf_node = self.nodes
        rbf_node_address = rbf_node.getnewaddress()

        # fund rbf node with 10 coins of 0.001 TPC (100,000 tapyrus)
        self.log.info("Mining blocks...")
        peer_node.generate(11, self.signblockprivkey_wif)
        self.sync_all()
        for i in range(25):
            peer_node.sendtoaddress(rbf_node_address, 0.001)
        self.sync_all()
        peer_node.generate(1, self.signblockprivkey_wif)
        self.sync_all()
        assert_equal(rbf_node.getbalance(), Decimal("0.025"))

        self.log.info("Running tests")
        dest_address = peer_node.getnewaddress()
        test_simple_bumpfee_succeeds(rbf_node, peer_node, dest_address)
        test_nonrbf_bumpfee_fails(peer_node, dest_address)
        test_notmine_bumpfee_fails(rbf_node, peer_node, dest_address)
        test_bumpfee_with_descendant_fails(rbf_node, rbf_node_address, dest_address)
        test_small_output_fails(rbf_node, dest_address)
        test_dust_to_fee(rbf_node, dest_address)
        test_settxfee(rbf_node, dest_address)
        test_rebumping(rbf_node, dest_address)
        test_rebumping_not_replaceable(rbf_node, dest_address)
        test_unconfirmed_not_spendable(rbf_node, rbf_node_address, self.signblockprivkey, self.signblockprivkey_wif)
        test_bumpfee_metadata(rbf_node, dest_address)
        test_bumpfee_preserves_colored_token_balance(rbf_node, peer_node, self.signblockprivkey_wif)
        test_bumpfee_no_tpc_change(rbf_node, peer_node, self.signblockprivkey_wif)
        test_locked_wallet_fails(rbf_node, dest_address)
        self.log.info("Success")


def test_simple_bumpfee_succeeds(rbf_node, peer_node, dest_address):
    rbfid = spend_one_input(rbf_node, dest_address)
    rbftx = rbf_node.gettransaction(rbfid)
    sync_mempools((rbf_node, peer_node))
    assert rbfid in rbf_node.getrawmempool() and rbfid in peer_node.getrawmempool()
    bumped_tx = rbf_node.bumpfee(rbfid)
    assert_equal(bumped_tx["errors"], [])
    assert bumped_tx["fee"] - abs(rbftx['details'][0]["fee"]) > 0
    # check that bumped_tx propagates, original tx was evicted and has a wallet conflict
    sync_mempools((rbf_node, peer_node))
    assert bumped_tx["txid"] in rbf_node.getrawmempool()
    assert bumped_tx["txid"] in peer_node.getrawmempool()
    assert rbfid not in rbf_node.getrawmempool()
    assert rbfid not in peer_node.getrawmempool()
    oldwtx = rbf_node.gettransaction(rbfid)
    assert len(oldwtx["walletconflicts"]) > 0
    # check wallet transaction replaces and replaced_by values
    bumpedwtx = rbf_node.gettransaction(bumped_tx["txid"])
    assert_equal(oldwtx["replaced_by_txid"], bumped_tx["txid"])
    assert_equal(bumpedwtx["replaces_txid"], rbfid)


def test_nonrbf_bumpfee_fails(peer_node, dest_address):
    # cannot replace a non RBF transaction (from node which did not enable RBF)
    not_rbfid = peer_node.sendtoaddress(dest_address, Decimal("0.00090000"))
    assert_raises_rpc_error(-4, "not BIP 125 replaceable", peer_node.bumpfee, not_rbfid)


def test_notmine_bumpfee_fails(rbf_node, peer_node, dest_address):
    # cannot bump fee unless the tx has only inputs that we own.
    # here, the rbftx has a peer_node coin and then adds a rbf_node input
    # Note that this test depends upon the RPC code checking input ownership prior to change outputs
    # (since it can't use fundrawtransaction, it lacks a proper change output)
    utxos = [node.listunspent()[-1] for node in (rbf_node, peer_node)]
    inputs = [{
        "txid": utxo["txid"],
        "vout": utxo["vout"],
        "address": utxo["address"],
        "sequence": BIP125_SEQUENCE_NUMBER
    } for utxo in utxos]
    output_val = sum(utxo["amount"] for utxo in utxos) - Decimal("0.001")
    rawtx = rbf_node.createrawtransaction(inputs, {dest_address: output_val})
    signedtx = rbf_node.signrawtransactionwithwallet(rawtx, [], "ALL", SCHEME)
    signedtx = peer_node.signrawtransactionwithwallet(signedtx["hex"], [], "ALL", SCHEME)
    rbfid = rbf_node.sendrawtransaction(signedtx["hex"])
    assert_raises_rpc_error(-4, "Transaction contains inputs that don't belong to this wallet",
                          rbf_node.bumpfee, rbfid)


def test_bumpfee_with_descendant_fails(rbf_node, rbf_node_address, dest_address):
    # cannot bump fee if the transaction has a descendant
    # parent is send-to-self, so we don't have to check which output is change when creating the child tx
    parent_id = spend_one_input(rbf_node, rbf_node_address)
    tx = rbf_node.createrawtransaction([{"txid": parent_id, "vout": 0}], {dest_address: 0.00020000})
    tx = rbf_node.signrawtransactionwithwallet(tx, [], "ALL", SCHEME)
    rbf_node.sendrawtransaction(tx["hex"])
    assert_raises_rpc_error(-8, "Transaction has descendants in the wallet", rbf_node.bumpfee, parent_id)


def test_small_output_fails(rbf_node, dest_address):
    # cannot bump fee with a too-small output
    rbfid = spend_one_input(rbf_node, dest_address)
    rbf_node.bumpfee(rbfid, {"totalFee": 50000})

    rbfid = spend_one_input(rbf_node, dest_address)
    assert_raises_rpc_error(-4, "Change output is too small", rbf_node.bumpfee, rbfid, {"totalFee": 50001})


def test_dust_to_fee(rbf_node, dest_address):
    # check that if output is reduced to dust, it will be converted to fee
    # the bumped tx sets fee=49,900, but it converts to 50,000
    rbfid = spend_one_input(rbf_node, dest_address)
    fulltx = rbf_node.getrawtransaction(rbfid, 1)
    # (32-byte p2sh-pwpkh output size + 148 p2pkh spend estimate) * 10k(discard_rate) / 1000 = 1800
    # P2SH outputs are slightly "over-discarding" due to the IsDust calculation assuming it will
    # be spent as a P2PKH.
    bumped_tx = rbf_node.bumpfee(rbfid, {"totalFee": 50000-1800})
    full_bumped_tx = rbf_node.getrawtransaction(bumped_tx["txid"], 1)
    assert_equal(bumped_tx["fee"], Decimal("0.00050000"))
    assert_equal(len(fulltx["vout"]), 2)
    assert_equal(len(full_bumped_tx["vout"]), 1)  #change output is eliminated


def test_settxfee(rbf_node, dest_address):
    # check that bumpfee reacts correctly to the use of settxfee (paytxfee)
    rbfid = spend_one_input(rbf_node, dest_address)
    requested_feerate = Decimal("0.00025000")
    rbf_node.settxfee(requested_feerate)
    bumped_tx = rbf_node.bumpfee(rbfid)
    actual_feerate = bumped_tx["fee"] * 1000 / rbf_node.getrawtransaction(bumped_tx["txid"], True)["size"]
    # Assert that the difference between the requested feerate and the actual
    # feerate of the bumped transaction is small.
    assert_greater_than(Decimal("0.00001000"), abs(requested_feerate - actual_feerate))
    rbf_node.settxfee(Decimal("0.00000000"))  # unset paytxfee


def test_rebumping(rbf_node, dest_address):
    # check that re-bumping the original tx fails, but bumping the bumper succeeds
    rbfid = spend_one_input(rbf_node, dest_address)
    bumped = rbf_node.bumpfee(rbfid, {"totalFee": 2000})
    assert_raises_rpc_error(-4, "already bumped", rbf_node.bumpfee, rbfid, {"totalFee": 3000})
    rbf_node.bumpfee(bumped["txid"], {"totalFee": 3000})


def test_rebumping_not_replaceable(rbf_node, dest_address):
    # check that re-bumping a non-replaceable bump tx fails
    rbfid = spend_one_input(rbf_node, dest_address)
    bumped = rbf_node.bumpfee(rbfid, {"totalFee": 10000, "replaceable": False})
    assert_raises_rpc_error(-4, "Transaction is not BIP 125 replaceable", rbf_node.bumpfee, bumped["txid"],
                          {"totalFee": 20000})


def test_unconfirmed_not_spendable(rbf_node, rbf_node_address, signblockprivkey, signblockprivkey_wif):
    # check that unconfirmed outputs from bumped transactions are not spendable
    rbfid = spend_one_input(rbf_node, rbf_node_address)
    rbftx = rbf_node.gettransaction(rbfid)["hex"]
    assert rbfid in rbf_node.getrawmempool()
    bumpid = rbf_node.bumpfee(rbfid)["txid"]
    assert bumpid in rbf_node.getrawmempool()
    assert rbfid not in rbf_node.getrawmempool()

    # check that outputs from the bump transaction are not spendable
    # due to the replaces_txid check in CWallet::AvailableCoins
    assert_equal([t for t in rbf_node.listunspent(include_unsafe=False) if t["txid"] == bumpid], [])

    # submit a block with the rbf tx to clear the bump tx out of the mempool,
    # then invalidate the block so the rbf tx will be put back in the mempool.
    # This makes it possible to check whether the rbf tx outputs are
    # spendable before the rbf tx is confirmed.
    block = submit_block_with_tx(rbf_node, rbftx, signblockprivkey)
    # Can not abandon conflicted tx
    assert_raises_rpc_error(-5, 'Transaction not eligible for abandonment', lambda: rbf_node.abandontransaction(txid=bumpid))
    rbf_node.invalidateblock(block.hash)
    # Call abandon to make sure the wallet doesn't attempt to resubmit
    # the bump tx and hope the wallet does not rebroadcast before we call.
    rbf_node.abandontransaction(bumpid)
    assert bumpid not in rbf_node.getrawmempool()
    assert rbfid in rbf_node.getrawmempool()

    # check that outputs from the rbf tx are not spendable before the
    # transaction is confirmed, due to the replaced_by_txid check in
    # CWallet::AvailableCoins
    assert_equal([t for t in rbf_node.listunspent(include_unsafe=False) if t["txid"] == rbfid], [])

    # check that the main output from the rbf tx is spendable after confirmed
    rbf_node.generate(1, signblockprivkey_wif)
    assert_equal(
        sum(1 for t in rbf_node.listunspent(include_unsafe=False)
            if t["txid"] == rbfid and t["address"] == rbf_node_address and t["spendable"]), 1)


def test_bumpfee_metadata(rbf_node, dest_address):
    rbfid = rbf_node.sendtoaddress(dest_address, Decimal("0.00100000"), "comment value", "to value")
    bumped_tx = rbf_node.bumpfee(rbfid)
    bumped_wtx = rbf_node.gettransaction(bumped_tx["txid"])
    assert_equal(bumped_wtx["comment"], "comment value")
    assert_equal(bumped_wtx["to"], "to value")


def test_locked_wallet_fails(rbf_node, dest_address):
    rbfid = spend_one_input(rbf_node, dest_address)
    rbf_node.walletlock()
    assert_raises_rpc_error(-13, "Please enter the wallet passphrase with walletpassphrase first.",
                          rbf_node.bumpfee, rbfid)


def test_bumpfee_preserves_colored_token_balance(rbf_node, peer_node, signblockprivkey_wif):
    """Regression for J2: bumpfee on a colored-token transfer must not silently
    subtract the fee delta from a colored output."""
    # Mine any leftover mempool txns from preceding tests.
    rbf_node.generate(1, signblockprivkey_wif)
    sync_mempools((rbf_node, peer_node))

    # Issue 100 NON_REISSUABLE tokens using a wallet TPC UTXO as the colorId seed.
    seed_utxo = max((u for u in rbf_node.listunspent() if u.get('token') == 'TPC'), key=lambda u: u['amount'])
    issue_result = rbf_node.issuetoken(2, 100, seed_utxo['txid'], seed_utxo['vout'])
    color = issue_result['color']

    # Propagate issuance to peer_node first, then mine it.
    sync_mempools((rbf_node, peer_node))
    peer_node.generate(1, signblockprivkey_wif)
    sync_mempools((rbf_node, peer_node))

    balance_before = rbf_node.getwalletinfo()['balance'][color]
    assert_equal(balance_before, 100)

    # Transfer 60 tokens to self (walletrbf=1 -> BIP125 replaceable).
    dest_addr = rbf_node.getnewaddress("", color)
    transfer_txid = rbf_node.transfertoken(dest_addr, 60)
    assert_equal(rbf_node.gettransaction(transfer_txid)['bip125-replaceable'], 'yes')

    # Propagate transfer to peer_node before bumping fee.
    sync_mempools((rbf_node, peer_node))

    # Bump the fee — only the TPC change output may shrink; colored outputs must be untouched.
    rbf_node.bumpfee(transfer_txid)

    sync_mempools((rbf_node, peer_node))
    peer_node.generate(1, signblockprivkey_wif)
    sync_mempools((rbf_node, peer_node))

    # Token balance must still be 100 (60 sent-to-self + 40 change).
    assert_equal(rbf_node.getwalletinfo()['balance'][color], 100)


def test_bumpfee_no_tpc_change(rbf_node, peer_node, signblockprivkey_wif):
    """Regression for H3: bumpfee on a colored transfer that has no TPC change output
    must add a new TPC input via the fAllowOtherInputs fallback path."""
    rbf_node.generate(1, signblockprivkey_wif)
    sync_mempools((rbf_node, peer_node))

    # Issue 100 tokens.
    seed_utxo = max((u for u in rbf_node.listunspent() if u.get('token') == 'TPC'), key=lambda u: u['amount'])
    issue_result = rbf_node.issuetoken(2, 100, seed_utxo['txid'], seed_utxo['vout'])
    color = issue_result['color']
    sync_mempools((rbf_node, peer_node))
    peer_node.generate(1, signblockprivkey_wif)
    sync_mempools((rbf_node, peer_node))

    # Create a small TPC UTXO that will be consumed entirely as fee in the colored transfer.
    small_tpc_addr = rbf_node.getnewaddress()
    small_tpc_txid = rbf_node.sendtoaddress(small_tpc_addr, Decimal("0.00005"))
    sync_mempools((rbf_node, peer_node))  # ensure peer_node sees the tx before mining
    peer_node.generate(1, signblockprivkey_wif)
    sync_mempools((rbf_node, peer_node))

    # Locate the two UTXOs to spend.
    token_utxo = next(u for u in rbf_node.listunspent() if u.get('token') == color)
    # Use getrawtransaction to find the vout index for the small TPC output by address.
    raw_small = rbf_node.getrawtransaction(small_tpc_txid, True)
    small_tpc_vout_idx = next(
        v['n'] for v in raw_small['vout']
        if small_tpc_addr in v.get('scriptPubKey', {}).get('addresses', [])
    )
    small_tpc_utxo = {'txid': small_tpc_txid, 'vout': small_tpc_vout_idx}

    # Build a colored transfer with no TPC change output: fee = entire small TPC input.
    colored_addr = rbf_node.getnewaddress("", color)
    rawtx = rbf_node.createrawtransaction(
        inputs=[
            {'txid': token_utxo['txid'], 'vout': token_utxo['vout'],
             'sequence': BIP125_SEQUENCE_NUMBER},
            {'txid': small_tpc_utxo['txid'], 'vout': small_tpc_utxo['vout'],
             'sequence': BIP125_SEQUENCE_NUMBER},
        ],
        outputs=[{colored_addr: 100}],
    )
    signed = rbf_node.signrawtransactionwithwallet(rawtx, [], "ALL", SCHEME)
    assert signed['complete'], "Failed to sign colored transfer with no TPC change"
    orig_txid = rbf_node.sendrawtransaction(signed['hex'])
    assert orig_txid in rbf_node.getrawmempool()

    sync_mempools((rbf_node, peer_node))

    # Capture original vin count before bumpfee replaces the tx in the mempool.
    orig_decoded = rbf_node.getrawtransaction(orig_txid, True)
    orig_vin_count = len(orig_decoded['vin'])

    # bumpfee must succeed by adding a new TPC input (fAllowOtherInputs fallback).
    bumped = rbf_node.bumpfee(orig_txid)
    assert_equal(bumped['errors'], [])

    bumped_decoded = rbf_node.getrawtransaction(bumped['txid'], True)
    assert_greater_than(len(bumped_decoded['vin']), orig_vin_count)

    # Mine and verify token balance is fully preserved.
    sync_mempools((rbf_node, peer_node))
    peer_node.generate(1, signblockprivkey_wif)
    sync_mempools((rbf_node, peer_node))

    assert_equal(rbf_node.getwalletinfo()['balance'][color], 100)


def spend_one_input(node, dest_address):
    tx_input = dict(
        sequence=BIP125_SEQUENCE_NUMBER, **next(u for u in node.listunspent() if u["amount"] == Decimal("0.00100000")))
    rawtx = node.createrawtransaction(
        [tx_input], {dest_address: Decimal("0.00050000"),
                     node.getrawchangeaddress(): Decimal("0.00049000")})
    signedtx = node.signrawtransactionwithwallet(rawtx, [], "ALL", SCHEME)
    txid = node.sendrawtransaction(signedtx["hex"])
    return txid


def submit_block_with_tx(node, tx, signblockprivkey):
    ctx = CTransaction()
    ctx.deserialize(io.BytesIO(hex_str_to_bytes(tx)))

    tip = node.getbestblockhash()
    height = node.getblockcount() + 1
    block_time = node.getblockheader(tip)["mediantime"] + 1
    block = create_block(int(tip, 16), create_coinbase(height), block_time)
    block.vtx.append(ctx)
    block.rehash()
    block.hashMerkleRoot = block.calc_merkle_root()
    block.hashImMerkleRoot = block.calc_immutable_merkle_root()
    block.solve(signblockprivkey)
    node.submitblock(bytes_to_hex_str(block.serialize()))
    return block


if __name__ == "__main__":
    BumpFeeTest().main()
