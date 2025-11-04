#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""  Tests the mempool:* tracepoint API interface.
     See https://github.com/bitcoin/bitcoin/blob/master/doc/tracing.md#context-mempool
"""

from decimal import Decimal
from bcc import BPF, USDT  # type: ignore[import]

from test_framework.messages import COIN
from test_framework.mininode import P2PDataStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, TAPYRUS_MODES
from test_framework.timeout_config import TAPYRUSD_SYNC_TIMEOUT

MEMPOOL_ADDED_PROGRAM = """
# include <uapi/linux/ptrace.h>

#define HASH_LENGTH 32

struct added_event
{
  u8    hash[HASH_LENGTH];
  s32   vsize;
  s64   fee;
};

BPF_PERF_OUTPUT(added_events);

int trace_added(struct pt_regs *ctx) {
  struct added_event added = {};

  bpf_usdt_readarg_p(1, ctx, &added.hash, HASH_LENGTH);
  bpf_usdt_readarg(2, ctx, &added.vsize);
  bpf_usdt_readarg(3, ctx, &added.fee);

  added_events.perf_submit(ctx, &added, sizeof(added));
  return 0;
}
"""

MEMPOOL_REMOVED_PROGRAM = """
# include <uapi/linux/ptrace.h>

#define MAX_REMOVAL_REASON_LENGTH 9
#define HASH_LENGTH 32

struct removed_event
{
  u8    hash[HASH_LENGTH];
  char  reason[MAX_REMOVAL_REASON_LENGTH];
  s32   vsize;
  s64   fee;
  u64   entry_time;
};

BPF_PERF_OUTPUT(removed_events);

int trace_removed(struct pt_regs *ctx) {
  struct removed_event removed = {};

  bpf_usdt_readarg_p(1, ctx, &removed.hash, HASH_LENGTH);
  bpf_usdt_readarg_p(2, ctx, &removed.reason, MAX_REMOVAL_REASON_LENGTH);
  bpf_usdt_readarg(3, ctx, &removed.vsize);
  bpf_usdt_readarg(4, ctx, &removed.fee);
  bpf_usdt_readarg(5, ctx, &removed.entry_time);

  removed_events.perf_submit(ctx, &removed, sizeof(removed));
  return 0;
}
"""

MEMPOOL_REJECTED_PROGRAM = """
# include <uapi/linux/ptrace.h>

#define MAX_REJECT_REASON_LENGTH 118
#define HASH_LENGTH 32

struct rejected_event
{
  u8    hash[HASH_LENGTH];
  char  reason[MAX_REJECT_REASON_LENGTH];
};

BPF_PERF_OUTPUT(rejected_events);

int trace_rejected(struct pt_regs *ctx) {
  struct rejected_event rejected = {};

  bpf_usdt_readarg_p(1, ctx, &rejected.hash, HASH_LENGTH);
  bpf_usdt_readarg_p(2, ctx, &rejected.reason, MAX_REJECT_REASON_LENGTH);

  rejected_events.perf_submit(ctx, &rejected, sizeof(rejected));
  return 0;
}
"""

MEMPOOL_REPLACED_PROGRAM = """
# include <uapi/linux/ptrace.h>

#define HASH_LENGTH 32

struct replaced_event
{
  u8    replaced_hash[HASH_LENGTH];
  s32   replaced_vsize;
  s64   replaced_fee;
  u64   replaced_entry_time;
  u8    replacement_hash[HASH_LENGTH];
  s32   replacement_vsize;
  s64   replacement_fee;
};

BPF_PERF_OUTPUT(replaced_events);

int trace_replaced(struct pt_regs *ctx) {
  struct replaced_event replaced = {};

  bpf_usdt_readarg_p(1, ctx, &replaced.replaced_hash, HASH_LENGTH);
  bpf_usdt_readarg(2, ctx, &replaced.replaced_vsize);
  bpf_usdt_readarg(3, ctx, &replaced.replaced_fee);
  bpf_usdt_readarg(4, ctx, &replaced.replaced_entry_time);
  bpf_usdt_readarg_p(5, ctx, &replaced.replacement_hash, HASH_LENGTH);
  bpf_usdt_readarg(6, ctx, &replaced.replacement_vsize);
  bpf_usdt_readarg(7, ctx, &replaced.replacement_fee);

  replaced_events.perf_submit(ctx, &replaced, sizeof(replaced));
  return 0;
}
"""


class MempoolTracepointTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.mode = TAPYRUS_MODES.PROD

    def skip_test_if_missing_module(self):
        self.skip_if_platform_not_linux()
        self.skip_if_no_bitcoind_tracepoints()
        self.skip_if_no_python_bcc()
        self.skip_if_no_bpf_permissions()

    def added_test(self):
        """Add a transaction to the mempool and make sure the tracepoint returns
        the expected txid, vsize, and fee."""

        EXPECTED_ADDED_EVENTS = 1
        handled_added_events = 0

        self.log.info("Hooking into mempool:added tracepoint...")
        node = self.nodes[0]
        ctx = USDT(pid=node.process.pid)
        ctx.enable_probe(probe="mempool:added", fn_name="trace_added")
        bpf = BPF(text=MEMPOOL_ADDED_PROGRAM, usdt_contexts=[ctx], debug=0)

        def handle_added_event(_, data, __):
            nonlocal handled_added_events
            event = bpf["added_events"].event(data)
            assert_equal(txid, bytes(event.hash)[::-1].hex())
            assert_equal(vsize, event.vsize)
            assert_equal(fee, event.fee)
            handled_added_events += 1

        bpf["added_events"].open_perf_buffer(handle_added_event)

        self.log.info("Sending transaction...")
        fee = Decimal(31200)
        tx = node.sendtoaddress(node.getnewaddress(), 1)
        # expected data
        txid = tx["txid"]
        vsize = tx["tx"].get_vsize()

        self.log.info("Polling buffer...")
        bpf.perf_buffer_poll(timeout=TAPYRUSD_SYNC_TIMEOUT)

        self.log.info("Cleaning up mempool...")
        node.generate(1, self.signblockprivkey_wif)

        bpf.cleanup()

        self.log.info("Ensuring mempool:added event was handled successfully...")
        assert_equal(EXPECTED_ADDED_EVENTS, handled_added_events)

    def removed_test(self):
        """Expire a transaction from the mempool and make sure the tracepoint returns
        the expected txid, expiry reason, vsize, and fee."""

        EXPECTED_REMOVED_EVENTS = 1
        handled_removed_events = 0

        self.log.info("Hooking into mempool:removed tracepoint...")
        node = self.nodes[0]
        ctx = USDT(pid=node.process.pid)
        ctx.enable_probe(probe="mempool:removed", fn_name="trace_removed")
        bpf = BPF(text=MEMPOOL_REMOVED_PROGRAM, usdt_contexts=[ctx], debug=0)

        def handle_removed_event(_, data, __):
            nonlocal handled_removed_events
            event = bpf["removed_events"].event(data)
            assert_equal(txid, bytes(event.hash)[::-1].hex())
            assert_equal(reason, event.reason.decode("UTF-8"))
            assert_equal(vsize, event.vsize)
            assert_equal(fee, event.fee)
            assert_equal(entry_time, event.entry_time)
            handled_removed_events += 1

        bpf["removed_events"].open_perf_buffer(handle_removed_event)

        self.log.info("Sending transaction...")
        fee = Decimal(31200)
        tx = node.sendtoaddress(node.getnewaddress(), 1)
        # expected data
        txid = tx["txid"]
        reason = "expiry"
        vsize = tx["tx"].get_vsize()

        self.log.info("Fast-forwarding time to mempool expiry...")
        entry_time = node.getmempoolentry(txid)["time"]
        expiry_time = entry_time + 60 * 60 + 5
        node.setmocktime(expiry_time)

        self.log.info("Triggering expiry...")
        node.gettransaction(txid=txid)
        tx = node.sendtoaddress(node.getnewaddress(), 1)

        self.log.info("Polling buffer...")
        bpf.perf_buffer_poll(timeout=TAPYRUSD_SYNC_TIMEOUT)

        bpf.cleanup()

        self.log.info("Ensuring mempool:removed event was handled successfully...")
        assert_equal(EXPECTED_REMOVED_EVENTS, handled_removed_events)

    def replaced_test(self):
        """Replace one and two transactions in the mempool and make sure the tracepoint
        returns the expected txids, vsizes, and fees."""

        EXPECTED_REPLACED_EVENTS = 1
        handled_replaced_events = 0

        self.log.info("Hooking into mempool:replaced tracepoint...")
        node = self.nodes[0]
        ctx = USDT(pid=node.process.pid)
        ctx.enable_probe(probe="mempool:replaced", fn_name="trace_replaced")
        bpf = BPF(text=MEMPOOL_REPLACED_PROGRAM, usdt_contexts=[ctx], debug=0)

        def handle_replaced_event(_, data, __):
            nonlocal handled_replaced_events
            event = bpf["replaced_events"].event(data)
            assert_equal(replaced_txid, bytes(event.replaced_hash)[::-1].hex())
            assert_equal(replaced_vsize, event.replaced_vsize)
            assert_equal(replaced_fee, event.replaced_fee)
            assert_equal(replaced_entry_time, event.replaced_entry_time)
            assert_equal(replacement_txid, bytes(event.replacement_hash)[::-1].hex())
            assert_equal(replacement_vsize, event.replacement_vsize)
            assert_equal(replacement_fee, event.replacement_fee)
            handled_replaced_events += 1

        bpf["replaced_events"].open_perf_buffer(handle_replaced_event)

        self.log.info("Sending RBF transaction...")
        utxo = self.wallet.get_utxo(mark_as_spent=True)
        original_fee = Decimal(40000)
        original_tx = tx = node.sendtoaddress(node.getnewaddress(), 1)
        entry_time = node.getmempoolentry(original_tx["txid"])["time"]

        self.log.info("Sending replacement transaction...")
        replacement_fee = Decimal(45000)
        replacement_tx = tx = node.sendtoaddress(node.getnewaddress(), 1)

        # expected data
        replaced_txid = original_tx["txid"]
        replaced_vsize = original_tx["tx"].get_vsize()
        replaced_fee = original_fee
        replaced_entry_time = entry_time
        replacement_txid = replacement_tx["txid"]
        replacement_vsize = replacement_tx["tx"].get_vsize()

        self.log.info("Polling buffer...")
        bpf.perf_buffer_poll(timeout=TAPYRUSD_SYNC_TIMEOUT)

        bpf.cleanup()

        self.log.info("Ensuring mempool:replaced event was handled successfully...")
        assert_equal(EXPECTED_REPLACED_EVENTS, handled_replaced_events)

    def rejected_test(self):
        """Create an invalid transaction and make sure the tracepoint returns
        the expected txid, rejection reason, peer id, and peer address."""

        EXPECTED_REJECTED_EVENTS = 1
        handled_rejected_events = 0

        self.log.info("Adding P2P connection...")
        node = self.nodes[0]
        node.add_p2p_connection(P2PDataStore())

        self.log.info("Hooking into mempool:rejected tracepoint...")
        ctx = USDT(pid=node.process.pid)
        ctx.enable_probe(probe="mempool:rejected", fn_name="trace_rejected")
        bpf = BPF(text=MEMPOOL_REJECTED_PROGRAM, usdt_contexts=[ctx], debug=0)

        def handle_rejected_event(_, data, __):
            nonlocal handled_rejected_events
            event = bpf["rejected_events"].event(data)
            assert_equal(txid, bytes(event.hash)[::-1].hex())
            assert_equal(reason, event.reason.decode("UTF-8"))
            handled_rejected_events += 1

        bpf["rejected_events"].open_perf_buffer(handle_rejected_event)

        self.log.info("Sending invalid transaction...")
        tx = self.wallet.create_self_transfer(fee_rate=Decimal(0))
        node.p2ps[0].send_txs_and_test([tx["tx"]], node, success=False)

        # expected data
        txid = tx["tx"].hash
        reason = "min relay fee not met"

        self.log.info("Polling buffer...")
        bpf.perf_buffer_poll(timeout=TAPYRUSD_SYNC_TIMEOUT)

        bpf.cleanup()

        self.log.info("Ensuring mempool:rejected event was handled successfully...")
        assert_equal(EXPECTED_REJECTED_EVENTS, handled_rejected_events)

    def run_test(self):
        """Tests the mempool:added, mempool:removed, mempool:replaced,
        and mempool:rejected tracepoints."""

        # Create some coinbase transactions and mature them so they can be spent
        node = self.nodes[0]
        node.generate(5, self.signblockprivkey_wif)

        # Test individual tracepoints
        self.added_test()
        self.removed_test()
        self.replaced_test()
        self.rejected_test()


if __name__ == "__main__":
    MempoolTracepointTest().main()
