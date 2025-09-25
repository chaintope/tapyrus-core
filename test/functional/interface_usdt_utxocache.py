#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

""" Tests the utxocache:* tracepoint API interface.
    See https://github.com/chaintope/tapyrus-core/blob/master/doc/tracing.md#context-utxocache
"""

import ctypes, io
# Test will be skipped if we don't have bcc installed
try:
    from bcc import BPF, USDT # type: ignore[import]
except ImportError:
    print
from test_framework.messages import COIN, CTransaction
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, hex_str_to_bytes, TAPYRUS_MODES
from test_framework.timeout_config import TAPYRUSD_SYNC_TIMEOUT

utxocache_add_program = """
#include <uapi/linux/ptrace.h>

typedef signed long long i64;

struct utxocache_change
{
    char        txid[64];
    u32         index;
    char        token[66];
    u32         height;
    i64         value;
    bool        is_coinbase;
};

BPF_PERF_OUTPUT(utxocache_add);
int trace_utxocache_add(struct pt_regs *ctx) {
    struct utxocache_change add = {};
    bpf_usdt_readarg_p(1, ctx, &add.txid, 64);
    bpf_usdt_readarg(2, ctx, &add.index);
    bpf_usdt_readarg_p(3, ctx, &add.token, 66);
    bpf_usdt_readarg(4, ctx, &add.height);
    bpf_usdt_readarg(5, ctx, &add.value);
    bpf_usdt_readarg(6, ctx, &add.is_coinbase);
    utxocache_add.perf_submit(ctx, &add, sizeof(add));
    return 0;
}
"""

utxocache_spent_program = """
#include <uapi/linux/ptrace.h>

typedef signed long long i64;

struct utxocache_change
{
    char        txid[64];
    u32         index;
    char        token[66];
    u32         height;
    i64         value;
    bool        is_coinbase;
};

BPF_PERF_OUTPUT(utxocache_spent);
int trace_utxocache_spent(struct pt_regs *ctx) {
    struct utxocache_change spent = {};
    bpf_usdt_readarg_p(1, ctx, &spent.txid, 64);
    bpf_usdt_readarg(2, ctx, &spent.index);
    bpf_usdt_readarg_p(3, ctx, &spent.token, 66);
    bpf_usdt_readarg(4, ctx, &spent.height);
    bpf_usdt_readarg(5, ctx, &spent.value);
    bpf_usdt_readarg(6, ctx, &spent.is_coinbase);
    utxocache_spent.perf_submit(ctx, &spent, sizeof(spent));
    return 0;
}
"""

utxocache_uncache_program = """
#include <uapi/linux/ptrace.h>

int trace_utxocache_uncache(struct pt_regs *ctx) {
    // Minimal probe - just attach, don't read arguments
    return 0;
}
"""

utxocache_add_spent_program = """
#include <uapi/linux/ptrace.h>

typedef signed long long i64;

struct utxocache_change
{
    char        txid[64];
    u32         index;
    char        token[66];
    u32         height;
    i64         value;
    bool        is_coinbase;
};

BPF_PERF_OUTPUT(utxocache_add);
int trace_utxocache_add(struct pt_regs *ctx) {
    struct utxocache_change add = {};
    bpf_usdt_readarg_p(1, ctx, &add.txid, 64);
    bpf_usdt_readarg(2, ctx, &add.index);
    bpf_usdt_readarg_p(3, ctx, &add.token, 66);
    bpf_usdt_readarg(4, ctx, &add.height);
    bpf_usdt_readarg(5, ctx, &add.value);
    bpf_usdt_readarg(6, ctx, &add.is_coinbase);
    utxocache_add.perf_submit(ctx, &add, sizeof(add));
    return 0;
}

BPF_PERF_OUTPUT(utxocache_spent);
int trace_utxocache_spent(struct pt_regs *ctx) {
    struct utxocache_change spent = {};
    bpf_usdt_readarg_p(1, ctx, &spent.txid, 64);
    bpf_usdt_readarg(2, ctx, &spent.index);
    bpf_usdt_readarg_p(3, ctx, &spent.token, 66);
    bpf_usdt_readarg(4, ctx, &spent.height);
    bpf_usdt_readarg(5, ctx, &spent.value);
    bpf_usdt_readarg(6, ctx, &spent.is_coinbase);
    utxocache_spent.perf_submit(ctx, &spent, sizeof(spent));
    return 0;
}
"""

utxocache_flushes_program = """
#include <uapi/linux/ptrace.h>

typedef signed long long i64;

struct utxocache_flush
{
    i64         duration;
    u32         mode;
    u64         size;
    u64         memory;
    bool        for_prune;
};

BPF_PERF_OUTPUT(utxocache_flush);
int trace_utxocache_flush(struct pt_regs *ctx) {
    struct utxocache_flush flush = {};
    bpf_usdt_readarg(1, ctx, &flush.duration);
    bpf_usdt_readarg(2, ctx, &flush.mode);
    bpf_usdt_readarg(3, ctx, &flush.size);
    bpf_usdt_readarg(4, ctx, &flush.memory);
    bpf_usdt_readarg(5, ctx, &flush.for_prune);
    utxocache_flush.perf_submit(ctx, &flush, sizeof(flush));
    return 0;
}
"""

FLUSHMODE_NAME = {
    0: "NONE",
    1: "IF_NEEDED",
    2: "PERIODIC",
    3: "ALWAYS",
}

token_name = lambda token_bytes : 'TPC' if bytes(token_bytes)[0:3] == b'TPC' else bytes(token_bytes).hex()

class UTXOCacheChange(ctypes.Structure):
    _fields_ = [
        ("txid", ctypes.c_ubyte * 64),
        ("index", ctypes.c_uint32),
        ("token", ctypes.c_ubyte * 66),
        ("height", ctypes.c_uint32),
        ("value", ctypes.c_uint64),
        ("is_coinbase", ctypes.c_bool),
    ]

    def __repr__(self):
        return f"UTXOCacheChange(outpoint={bytes(self.txid[0::]).decode()}:{self.index}, token={token_name(self.token)}, height={self.height}, value={self.value} tapyrus, is_coinbase={self.is_coinbase})"


class UTXOCacheFlush(ctypes.Structure):
    _fields_ = [
        ("duration", ctypes.c_int64),
        ("mode", ctypes.c_uint32),
        ("size", ctypes.c_uint64),
        ("memory", ctypes.c_uint64),
        ("for_prune", ctypes.c_bool),
    ]

    def __repr__(self):
        return f"UTXOCacheFlush(duration={self.duration}, mode={FLUSHMODE_NAME[self.mode]}, size={self.size}, memory={self.memory}, for_prune={self.for_prune})"


class UTXOCacheTracepointTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = False
        self.num_nodes = 1
        self.extra_args = [["-txindex"]]
        self.mode = TAPYRUS_MODES.PROD


    def run_test(self):
        self.nodes[0].generate(1, self.signblockprivkey_wif)

        self.test_uncache()
        self.test_add_spent()
        self.test_flush()

    def test_uncache(self):
        """ Tests the utxocache:uncache tracepoint API.
        https://github.com/chaintope/tapyrus-core/blob/master/doc/tracing.md#tracepoint-utxocacheuncache
        """
        # To trigger an UTXO uncache from the cache, we create an invalid transaction
        # spending a not-cached, but existing UTXO. During transaction validation, this
        # the UTXO is added to the utxo cache, but as the transaction is invalid, it's
        # uncached again.
        self.log.info("testing the utxocache:uncache tracepoint API")

        # Retrieve the txid for the UTXO created in the first block. This UTXO is not
        # in our UTXO cache.
        EARLY_BLOCK_HEIGHT = 1
        block_1_hash = self.nodes[0].getblockhash(EARLY_BLOCK_HEIGHT)
        block_1 = self.nodes[0].getblock(block_1_hash)
        block_1_coinbase_txid = block_1["tx"][0]

        # Create a transaction and invalidate it by changing the txid of the previous
        # output to the coinbase txid of the block at height 1.
        invalid_txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 10)
        tx = self.nodes[0].gettransaction(invalid_txid)
        invalid_tx = CTransaction()
        invalid_tx.deserialize(io.BytesIO(hex_str_to_bytes(tx['hex'])))
        invalid_tx.vin[0].prevout.hash = int(block_1_coinbase_txid, 16)

        self.log.info("hooking into the uncache tracepoint")
        ctx = USDT(pid=self.nodes[0].process.pid)
        ctx.enable_probe(probe="utxocache:utxocache_uncache",
                         fn_name="trace_utxocache_uncache")
        bpf = BPF(text=utxocache_uncache_program, usdt_contexts=[ctx], debug=0)

        # The handle_* function is a ctypes callback function called from C. When
        # we assert in the handle_* function, the AssertError doesn't propagate
        # back to Python. The exception is ignored. We manually count and assert
        # that the handle_* functions succeeded.
        EXPECTED_HANDLE_UNCACHE_SUCCESS = 1
        handle_uncache_succeeds = 0

        def handle_utxocache_uncache(_, data, __):
            nonlocal handle_uncache_succeeds
            event = ctypes.cast(data, ctypes.POINTER(UTXOCacheChange)).contents
            self.log.info(f"handle_utxocache_uncache(): {event}")
            assert_equal(block_1_coinbase_txid, bytes(event.txid[0::]).decode())
            assert_equal(0, event.index)  # prevout index
            assert_equal(EARLY_BLOCK_HEIGHT, event.height)
            assert_equal(50 * COIN, event.value)
            assert_equal(True, event.is_coinbase)

            handle_uncache_succeeds += 1

        bpf["utxocache_uncache"].open_perf_buffer(handle_utxocache_uncache)

        self.log.info(
            "testmempoolaccept the invalid transaction to trigger an UTXO-cache uncache")
        result = self.nodes[0].testmempoolaccept(
            [invalid_tx.serialize().hex()])[0]
        assert_equal(result["allowed"], False)

        bpf.perf_buffer_poll(timeout=TAPYRUSD_SYNC_TIMEOUT)
        bpf.cleanup()

        self.log.info(
            f"check that we successfully traced {EXPECTED_HANDLE_UNCACHE_SUCCESS} uncaches")
        assert_equal(EXPECTED_HANDLE_UNCACHE_SUCCESS, handle_uncache_succeeds)

    def test_add_spent(self):
        """ Tests the utxocache:add utxocache:utxocache_spent tracepoint API
            See https://github.com/chaintope/tapyrus-core/blob/master/doc/tracing.md#tracepoint-utxocacheadd
            and https://github.com/chaintope/tapyrus-core/blob/master/doc/tracing.md#tracepoint-utxocachespent
        """

        self.log.info(
            "test the utxocache:add and utxocache:utxocache_spent tracepoint API")

        self.log.info("create an unconfirmed transaction")
        node0_txid1 = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 10)

        # We mine a block to trace changes (add/spent) to the active in-memory cache
        # of the UTXO set (see CoinsTip() of CCoinsViewCache). However, in some cases
        # temporary clones of the active cache are made. For example, during mining with
        # the generate RPC call, the block is first tested in TestBlockValidity(). There,
        # a clone of the active cache is modified during a test ConnectBlock() call.
        # These are implementation details we don't want to test here. Thus, after
        # mining, we invalidate the block, start the tracing, and then trace the cache
        # changes to the active utxo cache.
        self.log.info("mine and invalidate a block that is later reconsidered")
        block_hash = self.nodes[0].generate(1, self.signblockprivkey_wif)[0]
        self.nodes[0].invalidateblock(block_hash)

        self.log.info(
            "hook into the utxocache:utxocache_add and utxocache:utxocache_spent tracepoints")
        ctx = USDT(pid = self.nodes[0].process.pid)
        ctx.enable_probe(probe="utxocache:utxocache_add", fn_name="trace_utxocache_add")
        ctx.enable_probe(probe="utxocache:utxocache_spent",
                         fn_name="trace_utxocache_spent")
        bpf = BPF(text=utxocache_add_spent_program, usdt_contexts=[ctx], debug=0)

        # The handle_* function is a ctypes callback function called from C. When
        # we assert in the handle_* function, the AssertError doesn't propagate
        # back to Python. The exception is ignored. We manually count and assert
        # that the handle_* functions succeeded.
        EXPECTED_HANDLE_ADD_SUCCESS = 5
        EXPECTED_HANDLE_SPENT_SUCCESS = 2
        handle_add_succeeds = 0
        handle_spent_succeeds = 0

        expected_utxocache_spents = []
        expected_utxocache_adds = []

        def handle_utxocache_add(_, data, __):
            nonlocal handle_add_succeeds
            event = ctypes.cast(data, ctypes.POINTER(UTXOCacheChange)).contents
            self.log.info(f"handle_utxocache_add(): {event}")
            add = expected_utxocache_adds.pop(0)
            assert_equal(add["txid"], bytes(event.txid[0::]).decode())
            assert_equal(add["index"], event.index)
            assert_equal(add["token"], token_name(event.token))
            assert_equal(add["height"], event.height)
            assert_equal(add["value"], event.value)
            assert_equal(add["is_coinbase"], event.is_coinbase)
            handle_add_succeeds += 1

        def handle_utxocache_spent(_, data, __):
            nonlocal handle_spent_succeeds
            event = ctypes.cast(data, ctypes.POINTER(UTXOCacheChange)).contents
            self.log.info(f"handle_utxocache_spent(): {event}")
            spent = expected_utxocache_spents.pop(0)
            assert_equal(spent["txid"], bytes(event.txid[0::]).decode())
            assert_equal(spent["index"], event.index)
            assert_equal(spent["token"], token_name(event.token))
            assert_equal(spent["height"], event.height)
            assert_equal(spent["value"], event.value)
            assert_equal(spent["is_coinbase"], event.is_coinbase)
            handle_spent_succeeds += 1

        bpf["utxocache_add"].open_perf_buffer(handle_utxocache_add)
        bpf["utxocache_spent"].open_perf_buffer(handle_utxocache_spent)

        # We trigger a block re-connection. This causes changes (add/spent)
        # to the UTXO-cache which in turn triggers the tracepoints.
        self.log.info("reconsider the previously invalidated block")
        self.nodes[0].reconsiderblock(block_hash)

        block = self.nodes[0].getblock(block_hash, 2)
        for (block_index, tx) in enumerate(block["tx"]):
            for vin in tx["vin"]:
                if "coinbase" not in vin:
                    prevout_tx = self.nodes[0].getrawtransaction(
                        vin["txid"], True)
                    prevout_tx_block = self.nodes[0].getblockheader(
                        prevout_tx["blockhash"])
                    spends_coinbase = "coinbase" in prevout_tx["vin"][0]
                    expected_utxocache_spents.append({
                        "txid": vin["txid"],
                        "index": vin["vout"],
                        "token": prevout_tx["vout"][vin["vout"]]["token"],
                        "height": prevout_tx_block["height"],
                        "value": int(prevout_tx["vout"][vin["vout"]]["value"] * COIN),
                        "is_coinbase": spends_coinbase,
                    })
            for (i, vout) in enumerate(tx["vout"]):
                if vout["scriptPubKey"]["type"] != "nulldata":
                    expected_utxocache_adds.append({
                        "txid": tx["txid"],
                        "index": i,
                        "token": vout["token"],
                        "height": block["height"],
                        "value": int(vout["value"] * COIN),
                        "is_coinbase": block_index == 0,
                    })

        assert_equal(EXPECTED_HANDLE_ADD_SUCCESS, len(expected_utxocache_adds))
        assert_equal(EXPECTED_HANDLE_SPENT_SUCCESS,
                     len(expected_utxocache_spents))

        bpf.perf_buffer_poll(timeout=TAPYRUSD_SYNC_TIMEOUT)
        bpf.cleanup()

        self.log.info(
            f"check that we successfully traced {EXPECTED_HANDLE_ADD_SUCCESS} adds and {EXPECTED_HANDLE_SPENT_SUCCESS} spent")
        assert_equal(0, len(expected_utxocache_adds))
        assert_equal(0, len(expected_utxocache_spents))
        assert_equal(EXPECTED_HANDLE_ADD_SUCCESS, handle_add_succeeds)
        assert_equal(EXPECTED_HANDLE_SPENT_SUCCESS, handle_spent_succeeds)

    def test_flush(self):
        """ Tests the utxocache:utxocache_flush tracepoint API.
            See https://github.com/chaintope/tapyrus-core/blob/master/doc/tracing.md#context-utxocache"""

        self.log.info("test the utxocache_flush tracepoint API")
        self.log.info("hook into the utxocache_flush tracepoint")
        ctx = USDT(pid = self.nodes[0].process.pid)
        ctx.enable_probe(probe="utxocache:utxocache_flush",
                         fn_name="trace_utxocache_flush")
        bpf = BPF(text=utxocache_flushes_program, usdt_contexts=[ctx], debug=0)

        # The handle_* function is a ctypes callback function called from C. When
        # we assert in the handle_* function, the AssertError doesn't propagate
        # back to Python. The exception is ignored. We manually count and assert
        # that the handle_* functions succeeded.
        EXPECTED_HANDLE_FLUSH_SUCCESS = 8
        handle_flush_succeeds = 0
        possible_cache_sizes = set()
        expected_flushes = []

        def handle_utxocache_flush(_, data, __):
            nonlocal handle_flush_succeeds
            event = ctypes.cast(data, ctypes.POINTER(UTXOCacheFlush)).contents
            self.log.info(f"handle_utxocache_flush(): {event}")
            expected = expected_flushes.pop(0)
            assert_equal(expected["mode"], FLUSHMODE_NAME[event.mode])
            possible_cache_sizes.remove(event.size)  # fails if size not in set
            # sanity checks only
            assert(event.memory > 0)
            assert(event.duration > 0)
            handle_flush_succeeds += 1

        bpf["utxocache_flush"].open_perf_buffer(handle_utxocache_flush)

        self.log.info("stop the node to flush the UTXO cache")
        UTXOS_IN_CACHE = 104  # might need to be changed if the eariler tests are modified
        # A node shutdown causes two flushes. One that flushes UTXOS_IN_CACHE
        # UTXOs and one that flushes 0 UTXOs. Normally the 0-UTXO-flush is the
        # second flush, however it can happen that the order changes.
        possible_cache_sizes = {UTXOS_IN_CACHE, 0}
        flush_for_shutdown = {"mode": "ALWAYS", "for_prune": False}
        expected_flushes.extend([flush_for_shutdown, flush_for_shutdown])
        self.stop_node(0)

        bpf.perf_buffer_poll(timeout=TAPYRUSD_SYNC_TIMEOUT)

        self.log.info("check that we don't expect additional flushes")
        assert_equal(0, len(expected_flushes))
        assert_equal(0, len(possible_cache_sizes))

        self.log.info("restart the node with -prune")
        self.start_node(0, ["-fastprune=1", "-prune=1"])

        BLOCKS_TO_MINE = 350
        self.log.info(f"mine {BLOCKS_TO_MINE} blocks to be able to prune")
        self.generate(self.wallet, BLOCKS_TO_MINE)
        # we added BLOCKS_TO_MINE coinbase UTXOs to the cache
        possible_cache_sizes = {BLOCKS_TO_MINE}
        expected_flushes.append(
            {"mode": "NONE", "for_prune": True, "size_fn": lambda x: x == BLOCKS_TO_MINE})

        self.log.info(f"prune blockchain to trigger a flush for pruning")
        self.nodes[0].pruneblockchain(315)

        bpf.perf_buffer_poll(timeout=TAPYRUSD_SYNC_TIMEOUT)
        bpf.cleanup()

        self.log.info(
            f"check that we don't expect additional flushes and that the handle_* function succeeded")
        assert_equal(0, len(expected_flushes))
        assert_equal(0, len(possible_cache_sizes))
        assert_equal(EXPECTED_HANDLE_FLUSH_SUCCESS, handle_flush_succeeds)


if __name__ == '__main__':
    UTXOCacheTracepointTest().main()
