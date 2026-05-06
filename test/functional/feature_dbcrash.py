#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test recovery from a crash during chainstate writing.

- 4 nodes
  * node0, node1, and node2 will have different dbcrash ratios, and different
    dbcache sizes
  * node3 will be a regular node, with no crashing.
  * The nodes will not connect to each other.

- use default test framework starting chain. initialize starting_tip_height to
  tip height.

- Main loop:
  * generate lots of TPC transactions on node3, enough to fill up a block.
  * generate colored-coin operations on node3 (issue NON_REISSUABLE + NFT,
    transfer confirmed colored UTXOs, burn confirmed colored UTXOs).  These
    exercise the WriteIssuedColorIdBatch / EraseIssuedColorId paths in the
    chainstate DB across crash boundaries.
  * uniformly randomly pick a tip height from starting_tip_height to
    tip_height; with probability 1/(height_difference+4), invalidate this block.
  * mine enough blocks to overtake tip_height at start of loop.
  * for each node in [node0,node1,node2]:
     - for each mined block:
       * submit block to node
       * if node crashed on/after submitting:
         - restart until recovery succeeds
         - check that utxo matches node3 using gettxoutsetinfo"""

import errno
import http.client
import random
import sys
import time

from test_framework.messages import COIN, COutPoint, CTransaction, CTxIn, CTxOut, ToHex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, create_confirmed_utxos, hex_str_to_bytes
from test_framework.timeout_config import TAPYRUSD_P2P_TIMEOUT, TAPYRUSD_PROC_TIMEOUT

# Intentionally tiny — overrides the default 32 MiB (bitcoin/bitcoin#31645) to
# force frequent small write batches, maximising crash opportunities in this test.
CRASH_TEST_BATCH_SIZE = 200000

HTTP_DISCONNECT_ERRORS = [http.client.CannotSendRequest]
try:
    HTTP_DISCONNECT_ERRORS.append(http.client.RemoteDisconnected)
except AttributeError:
    pass

class ChainstateWriteCrashTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        self.setup_clean_chain = False

        # Set -maxmempool=0 to turn off mempool memory sharing with dbcache
        # Set -rpcservertimeout=900 to reduce socket disconnects in this
        # long-running test
        self.base_args = ["-limitdescendantsize=0", "-maxmempool=0", "-rpcservertimeout=900", f"-dbbatchsize={CRASH_TEST_BATCH_SIZE}"]

        # Set different crash ratios and cache sizes.  Note that not all of
        # -dbcache goes to pcoinsTip.
        self.node0_args = ["-dbcrashratio=8", "-dbcache=4"] + self.base_args
        self.node1_args = ["-dbcrashratio=16", "-dbcache=8"] + self.base_args
        self.node2_args = ["-dbcrashratio=24", "-dbcache=16"] + self.base_args

        # Node3 is a normal node with default args, except will mine full blocks
        self.node3_args = ["-blockmaxsize=1000000"]
        self.extra_args = [self.node0_args, self.node1_args, self.node2_args, self.node3_args]

    def setup_network(self):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args)
        self.start_nodes()
        # Leave them unconnected, we'll use submitblock directly in this test

    def restart_node(self, node_index, expected_tip):
        """Start up a given node id, wait for the tip to reach the given block hash, and calculate the utxo hash.

        Exceptions on startup should indicate node crash (due to -dbcrashratio), in which case we try again. Give up
        after 60 seconds. Returns the utxo hash of the given node."""

        time_start = time.time()
        while time.time() - time_start < TAPYRUSD_PROC_TIMEOUT:
            try:
                # Any of these RPC calls could throw due to node crash
                self.start_node(node_index)
                self.nodes[node_index].waitforblock(expected_tip)
                utxo_hash = self.nodes[node_index].gettxoutsetinfo()['hash_serialized_3']
                return utxo_hash
            except:
                # An exception here should mean the node is about to crash.
                # If bitcoind exits, then try again.  wait_for_node_exit()
                # should raise an exception if bitcoind doesn't exit.
                self.wait_for_node_exit(node_index, timeout=TAPYRUSD_P2P_TIMEOUT)
            self.crashed_on_restart += 1
            time.sleep(1)

        # If we got here, bitcoind isn't coming back up on restart.  Could be a
        # bug in bitcoind, or we've gotten unlucky with our dbcrash ratio --
        # perhaps we generated a test case that blew up our cache?
        # TODO: If this happens a lot, we should try to restart without -dbcrashratio
        # and make sure that recovery happens.
        raise AssertionError("Unable to successfully restart node %d in allotted time", node_index)

    def submit_block_catch_error(self, node_index, block):
        """Try submitting a block to the given node.

        Catch any exceptions that indicate the node has crashed.
        Returns true if the block was submitted successfully; false otherwise."""

        try:
            self.nodes[node_index].submitblock(block)
            return True
        except http.client.BadStatusLine as e:
            # Prior to 3.5 BadStatusLine('') was raised for a remote disconnect error.
            if sys.version_info[0] == 3 and sys.version_info[1] < 5 and e.line == "''":
                self.log.debug("node %d submitblock raised exception: %s", node_index, e)
                return False
            else:
                raise
        except tuple(HTTP_DISCONNECT_ERRORS) as e:
            self.log.debug("node %d submitblock raised exception: %s", node_index, e)
            return False
        except OSError as e:
            self.log.debug("node %d submitblock raised OSError exception: errno=%s", node_index, e.errno)
            if e.errno in [errno.EPIPE, errno.ECONNREFUSED, errno.ECONNRESET, errno.EINVAL]:
                # The node has likely crashed
                return False
            else:
                # Unexpected exception, raise
                raise

    def sync_node3blocks(self, block_hashes):
        """Use submitblock to sync node3's chain with the other nodes

        If submitblock fails, restart the node and get the new utxo hash.
        If any nodes crash while updating, we'll compare utxo hashes to
        ensure recovery was successful."""

        node3_utxo_hash = self.nodes[3].gettxoutsetinfo()['hash_serialized_3']

        # Retrieve all the blocks from node3
        blocks = []
        for block_hash in block_hashes:
            blocks.append([block_hash, self.nodes[3].getblock(block_hash, 0)])

        # Deliver each block to each other node
        for i in range(3):
            nodei_utxo_hash = None
            self.log.debug("Syncing blocks to node %d", i)
            for (block_hash, block) in blocks:
                # Get the block from node3, and submit to node_i
                self.log.debug("submitting block %s", block_hash)
                if not self.submit_block_catch_error(i, block):
                    # TODO: more carefully check that the crash is due to -dbcrashratio
                    # (change the exit code perhaps, and check that here?)
                    self.wait_for_node_exit(i, timeout=TAPYRUSD_P2P_TIMEOUT)
                    self.log.debug("Restarting node %d after block hash %s", i, block_hash)
                    nodei_utxo_hash = self.restart_node(i, block_hash)
                    assert nodei_utxo_hash is not None
                    self.restart_counts[i] += 1
                else:
                    # Clear it out after successful submitblock calls -- the cached
                    # utxo hash will no longer be correct
                    nodei_utxo_hash = None

            # Check that the utxo hash matches node3's utxo set
            # NOTE: we only check the utxo set if we had to restart the node
            # after the last block submitted:
            # - checking the utxo hash causes a cache flush, which we don't
            # want to do every time; so
            # - we only update the utxo cache after a node restart, since flushing
            # the cache is a no-op at that point
            if nodei_utxo_hash is not None:
                self.log.debug("Checking txoutsetinfo matches for node %d", i)
                assert_equal(nodei_utxo_hash, node3_utxo_hash)

    def verify_utxo_hash(self):
        """Verify that the utxo hash of each node matches node3.

        Restart any nodes that crash while querying."""
        node3_utxo_hash = self.nodes[3].gettxoutsetinfo()['hash_serialized_3']
        self.log.info("Verifying utxo hash matches for all nodes: " +  node3_utxo_hash)

        for i in range(3):
            try:
                nodei_utxo_hash = self.nodes[i].gettxoutsetinfo()['hash_serialized_3']
            except OSError:
                # probably a crash on db flushing
                nodei_utxo_hash = self.restart_node(i, self.nodes[3].getbestblockhash())
            assert_equal(nodei_utxo_hash, node3_utxo_hash)

    def generate_small_transactions(self, node, count, utxo_list):
        # Derive fee and dust threshold from the node's current relay fee rate.
        relay_fee_tpc_per_kb = node.getnetworkinfo()['relayfee']
        relay_fee_sat_per_byte = max(1, int(relay_fee_tpc_per_kb * COIN / 1000))
        # Actual size: 4 (ver) + 1 + 2*148 (inputs) + 1 + 3*34 (outputs) + 4 (locktime) = 408 bytes
        TX_SIZE = 408
        FEE = relay_fee_sat_per_byte * TX_SIZE
        # Dust limit for a P2PKH output: 3 * fee_rate * (34-byte output + 148-byte spend cost)
        dust_threshold = 3 * relay_fee_sat_per_byte * (34 + 148)

        # Filter out UTXOs too small to produce non-dust outputs when combined 2→3.
        # Two combined UTXOs must cover FEE and yield output_amount >= dust_threshold.
        min_utxo = (3 * dust_threshold + FEE + 1) // 2
        utxo_list[:] = [u for u in utxo_list if int(u['amount'] * COIN) >= min_utxo]

        num_transactions = 0
        random.shuffle(utxo_list)
        while len(utxo_list) >= 2 and num_transactions < count:
            tx = CTransaction()
            input_amount = 0
            for i in range(2):
                utxo = utxo_list.pop()
                tx.vin.append(CTxIn(COutPoint(int(utxo['txid'], 16), utxo['vout'])))
                input_amount += int(utxo['amount'] * COIN)
            output_amount = (input_amount - FEE) // 3

            if output_amount < dust_threshold:
                # Skip if outputs would be below dust threshold or non-positive
                continue

            for i in range(3):
                tx.vout.append(CTxOut(output_amount, hex_str_to_bytes(utxo['scriptPubKey'])))

            # Sign and send the transaction to get into the mempool
            tx_signed_hex = node.signrawtransactionwithwallet(ToHex(tx), [], "ALL", self.options.scheme)['hex']
            node.sendrawtransaction(tx_signed_hex)
            num_transactions += 1

    def generate_colored_transactions(self, node, count):
        """Issue, transfer, and burn colored coins on node3.

        count controls how many of each operation to attempt.  The wallet's
        coin selection avoids double-spending in-mempool outputs automatically,
        so this is safe to call right after generate_small_transactions.

        Operations:
          - Issue count REISSUABLE tokens (type=1, 100 tokens each)
          - Issue count NON_REISSUABLE tokens (type=2, 10 tokens each)
          - Issue count NFT tokens (type=3, 1 token each)
          - Transfer up to count confirmed colored UTXOs to new wallet addresses
          - Burn up to count confirmed colored UTXOs

        Failures are logged and skipped — e.g. in early iterations there are
        no confirmed colored UTXOs yet, so transfer/burn loops are no-ops.
        """
        # Issue REISSUABLE tokens; colorId is derived from the TPC UTXO's scriptPubKey.
        for _ in range(count):
            tpc = next((u for u in node.listunspent() if u['token'] == 'TPC'), None)
            if tpc is None:
                break
            try:
                node.issuetoken(1, 100, tpc['scriptPubKey'])
            except Exception as e:
                self.log.debug("issuetoken REISSUABLE: %s", e)

        # Issue NON_REISSUABLE tokens; each call spends one TPC UTXO.
        for _ in range(count):
            tpc = next((u for u in node.listunspent() if u['token'] == 'TPC'), None)
            if tpc is None:
                break
            try:
                node.issuetoken(2, 10, tpc['txid'], tpc['vout'])
            except Exception as e:
                self.log.debug("issuetoken NON_REISSUABLE: %s", e)

        # Issue NFT tokens (amount must be 1).
        for _ in range(count):
            tpc = next((u for u in node.listunspent() if u['token'] == 'TPC'), None)
            if tpc is None:
                break
            try:
                node.issuetoken(3, 1, tpc['txid'], tpc['vout'])
            except Exception as e:
                self.log.debug("issuetoken NFT: %s", e)

        # Transfer confirmed colored UTXOs to fresh wallet addresses.
        # listunspent() here already excludes UTXOs spent by the issue calls above.
        colored = [u for u in node.listunspent() if u['token'] != 'TPC']
        for utxo in colored[:count]:
            try:
                addr = node.getnewaddress(color=utxo['token'])
                node.sendtoaddress(addr, utxo['amount'])
            except Exception as e:
                self.log.debug("colored transfer: %s", e)

        # Burn confirmed colored UTXOs not yet spent by transfers above.
        colored = [u for u in node.listunspent() if u['token'] != 'TPC']
        for utxo in colored[:count]:
            try:
                node.burntoken(utxo['token'], utxo['amount'])
            except Exception as e:
                self.log.debug("burntoken: %s", e)

    def generate_colorid_heavy_block(self, node, num_issuances_each):
        """Issue num_issuances_each NON_REISSUABLE and NFT tokens in a single block.

        With -dbbatchsize=100000 each issuance contributes ~2-3 dirty coin entries
        (~150 bytes each) to pcoinsTip.  Issuing 150+150 tokens yields ~900 dirty
        entries (~135 KB) which ensures BatchWrite's UTXO loop splits mid-way
        before the final batch that writes m_colorid_state->CommitToBatch, covering
        both crash-before and crash-after the colorId commit boundary.

        Returns the hash of the single mined block containing all issuances.
        """
        self.log.info(
            "Generating colorid-heavy block: %d NON_REISSUABLE + %d NFT",
            num_issuances_each, num_issuances_each)

        nr_count = 0
        for _ in range(num_issuances_each):
            tpc = next((u for u in node.listunspent() if u['token'] == 'TPC'), None)
            if tpc is None:
                self.log.warning("Ran out of TPC UTXOs at NON_REISSUABLE #%d", nr_count)
                break
            try:
                node.issuetoken(2, 10, tpc['txid'], tpc['vout'])
                nr_count += 1
            except Exception as e:
                self.log.debug("issuetoken NON_REISSUABLE failed: %s", e)

        nft_count = 0
        for _ in range(num_issuances_each):
            tpc = next((u for u in node.listunspent() if u['token'] == 'TPC'), None)
            if tpc is None:
                self.log.warning("Ran out of TPC UTXOs at NFT #%d", nft_count)
                break
            try:
                node.issuetoken(3, 1, tpc['txid'], tpc['vout'])
                nft_count += 1
            except Exception as e:
                self.log.debug("issuetoken NFT failed: %s", e)

        self.log.info("Mining %d NON_REISSUABLE + %d NFT into one block", nr_count, nft_count)
        block_hashes = node.generate(1, self.signblockprivkey_wif)
        return block_hashes[0]

    def run_test(self):
        # Track test coverage statistics
        self.restart_counts = [0, 0, 0]  # Track the restarts for nodes 0-2
        self.crashed_on_restart = 0      # Track count of crashes during recovery

        # Start by creating a lot of utxos on node3
        initial_height = self.nodes[3].getblockcount()
        utxo_list = create_confirmed_utxos(self.nodes[3].getnetworkinfo()['relayfee'], self.nodes[3], 2000, self.signblockprivkey_wif)
        self.log.info("Prepped %d utxo entries", len(utxo_list))

        # Sync these blocks with the other nodes
        block_hashes_to_sync = []
        for height in range(initial_height + 1, self.nodes[3].getblockcount() + 1):
            block_hashes_to_sync.append(self.nodes[3].getblockhash(height))

        self.log.debug("Syncing %d blocks with other nodes", len(block_hashes_to_sync))
        # Syncing the blocks could cause nodes to crash, so the test begins here.
        self.sync_node3blocks(block_hashes_to_sync)

        starting_tip_height = self.nodes[3].getblockcount()

        # Main test loop:
        # each time through the loop, generate a bunch of transactions,
        # and then either mine a single new block on the tip, or some-sized reorg.
        for i in range(40):
            self.log.info("Iteration %d, generating 1000 TPC + colored transactions %s", i, self.restart_counts)
            # Generate colored coin operations first, while confirmed TPC UTXOs are
            # still available.  generate_small_transactions would otherwise spend every
            # confirmed UTXO into the mempool before colored tx can find any.
            self.generate_colored_transactions(self.nodes[3], 3)
            # Refresh utxo_list so generate_small_transactions doesn't attempt to
            # double-spend inputs already consumed by generate_colored_transactions.
            utxo_list = [u for u in self.nodes[3].listunspent() if u['token'] == 'TPC']
            # Generate a bunch of small-ish transactions with remaining UTXOs
            self.generate_small_transactions(self.nodes[3], 1000, utxo_list)
            # Pick a random block between current tip, and starting tip
            current_height = self.nodes[3].getblockcount()
            random_height = random.randint(starting_tip_height, current_height)
            self.log.debug("At height %d, considering height %d", current_height, random_height)
            if random_height > starting_tip_height:
                # Randomly reorg from this point with some probability (1/4 for
                # tip, 1/5 for tip-1, ...)
                if random.random() < 1.0 / (current_height + 4 - random_height):
                    self.log.debug("Invalidating block at height %d", random_height)
                    self.nodes[3].invalidateblock(self.nodes[3].getblockhash(random_height))

            # Now generate new blocks until we pass the old tip height
            self.log.debug("Mining longer tip")
            block_hashes = []
            while current_height + 1 > self.nodes[3].getblockcount():
                block_hashes.extend(self.nodes[3].generate(min(10, current_height + 1 - self.nodes[3].getblockcount()), self.signblockprivkey_wif))
            self.log.debug("Syncing %d new blocks...", len(block_hashes))
            self.sync_node3blocks(block_hashes)
            utxo_list = [u for u in self.nodes[3].listunspent() if u['token'] == 'TPC']
            self.log.debug("Node3 utxo count: %d", len(utxo_list))

            # Stop early once both coverage conditions are satisfied.
            # This avoids running extra slow iterations on a long chain.
            if self.restart_counts != [0, 0, 0] and self.crashed_on_restart > 0:
                self.log.info("Coverage achieved after %d iterations, stopping early", i + 1)
                break

        # Check that the utxo hashes agree with node3
        # Useful side effect: each utxo cache gets flushed here, so that we
        # won't get crashes on shutdown at the end of the test.
        self.verify_utxo_hash()

        # Check the test coverage
        self.log.info("Restarted nodes: %s; crashes on restart: %d", self.restart_counts, self.crashed_on_restart)

        # If no nodes were restarted, we didn't test anything.
        assert self.restart_counts != [0, 0, 0]

        # Make sure we tested the case of crash-during-recovery.
        assert self.crashed_on_restart > 0

        # Warn if any of the nodes escaped restart.
        for i in range(3):
            if self.restart_counts[i] == 0:
                self.log.warning("Node %d never crashed during utxo flush!", i)

        # --- Colorid-heavy single-block crash test ---
        # Issue 150 NON_REISSUABLE + 150 NFT tokens into a single block.
        # Each issuance spends one defining TPC UTXO and creates one colored
        # output, contributing ~2-3 dirty coin entries (~150 bytes each) to
        # pcoinsTip.  With -dbbatchsize=100000 this creates ~135 KB of dirty
        # state so BatchWrite's UTXO loop flushes at least one partial batch
        # (crash-before the colorId commit) before writing the final batch
        # that calls m_colorid_state->CommitToBatch (crash-after).
        # sync_node3blocks drives the block through all crash nodes;
        # verify_utxo_hash confirms colorId state matches node3 after every
        # crash+recovery cycle.
        self.log.info("=== Colorid-heavy block crash test ===")
        self.restart_counts = [0, 0, 0]
        self.crashed_on_restart = 0

        # Create fresh TPC UTXOs on node3 (one per planned issuance + buffer),
        # then sync all preparation blocks to the crash nodes.
        pre_heavy_height = self.nodes[3].getblockcount()
        create_confirmed_utxos(self.nodes[3].getnetworkinfo()['relayfee'],
                               self.nodes[3], 350, self.signblockprivkey_wif)
        prep_hashes = [self.nodes[3].getblockhash(h)
                       for h in range(pre_heavy_height + 1, self.nodes[3].getblockcount() + 1)]
        self.log.debug("Syncing %d prep blocks for colorid-heavy test", len(prep_hashes))
        self.sync_node3blocks(prep_hashes)

        # Mine all 300 issuances into a single block and push it through the
        # crash nodes.
        colorid_block_hash = self.generate_colorid_heavy_block(self.nodes[3], 150)
        self.log.info("Colorid-heavy block hash: %s", colorid_block_hash)
        self.sync_node3blocks([colorid_block_hash])
        self.verify_utxo_hash()
        self.log.info(
            "Colorid-heavy block result: restarts=%s crashed_on_restart=%d",
            self.restart_counts, self.crashed_on_restart)

if __name__ == "__main__":
    ChainstateWriteCrashTest().main()
