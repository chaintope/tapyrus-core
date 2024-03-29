Example scripts for User-space, Statically Defined Tracing (USDT)
=================================================================

This directory contains scripts showcasing User-space, Statically Defined
Tracing (USDT) support for Tapyrus Core on Linux using. For more information on
USDT support in Tapyrus Core see the [USDT documentation].

[USDT documentation]: ../../doc/tracing.md


Examples for the two main eBPF front-ends, [bpftrace] and
[BPF Compiler Collection (BCC)], with support for USDT, are listed. BCC is used
for complex tools and daemons and `bpftrace` is preferred for one-liners and
shorter scripts.

[bpftrace]: https://github.com/iovisor/bpftrace
[BPF Compiler Collection (BCC)]: https://github.com/iovisor/bcc


To develop and run bpftrace and BCC scripts you need to install the
corresponding packages. See [installing bpftrace] and [installing BCC] for more
information. For development there exist a [bpftrace Reference Guide], a
[BCC Reference Guide], and a [bcc Python Developer Tutorial].

[installing bpftrace]: https://github.com/iovisor/bpftrace/blob/master/INSTALL.md
[installing BCC]: https://github.com/iovisor/bcc/blob/master/INSTALL.md
[bpftrace Reference Guide]: https://github.com/iovisor/bpftrace/blob/master/docs/reference_guide.md
[BCC Reference Guide]: https://github.com/iovisor/bcc/blob/master/docs/reference_guide.md
[bcc Python Developer Tutorial]: https://github.com/iovisor/bcc/blob/master/docs/tutorial_bcc_python_developer.md

## Examples

The bpftrace examples contain a relative path to the `tapyrusd` binary. By
default, the scripts should be run from the repository-root and assume a
self-compiled `tapyrusd` binary. The paths in the examples can be changed, for
example, to point to release builds if needed. See the
[Tapyrus Core USDT documentation] on how to list available tracepoints in your
`tapyrusd` binary.

[Tapyrus Core USDT documentation]: ../../doc/tracing.md#listing-available-tracepoints

**WARNING: eBPF programs require root privileges to be loaded into a Linux
kernel VM. This means the bpftrace and BCC examples must be executed with root
privileges. Make sure to carefully review any scripts that you run with root
privileges first!**

### log_p2p_traffic.bt

A bpftrace script logging information about inbound and outbound P2P network
messages. Based on the `net:inbound_message` and `net:outbound_message`
tracepoints.

By default, `bpftrace` limits strings to 64 bytes due to the limited stack size
in the eBPF VM. For example, Tor v3 addresses exceed the string size limit which
results in the port being cut off during logging. The string size limit can be
increased with the `BPFTRACE_STRLEN` environment variable (`BPFTRACE_STRLEN=70`
works fine).

```
$ bpftrace contrib/tracing/log_p2p_traffic.bt
```

Output
```
outbound 'ping' msg to peer 11 (Out-----M-Tx, [2a02:b10c:f747:1:ef:fake:ipv6:addr]:8333) with 8 bytes
inbound 'pong' msg from peer 11 (Out-----M-Tx, [2a02:b10c:f747:1:ef:fake:ipv6:addr]:8333) with 8 bytes
inbound 'inv' msg from peer 16 (Out-----M-Tx, XX.XX.XXX.121:8333) with 37 bytes
outbound 'getdata' msg to peer 16 (Out-----M-Tx, XX.XX.XXX.121:8333) with 37 bytes
inbound 'tx' msg from peer 16 (Out-----M-Tx, XX.XX.XXX.121:8333) with 222 bytes
outbound 'inv' msg to peer 9 (Out-----M-Tx, faketorv3addressa2ufa6odvoi3s77j4uegey0xb10csyfyve2t33curbyd.onion:8333) with 37 bytes
outbound 'inv' msg to peer 7 (Out-----M-Tx, XX.XX.XXX.242:8333) with 37 bytes
…
```

### p2p_monitor.py

A BCC Python script using curses for an interactive P2P message monitor. Based
on the `net:inbound_message` and `net:outbound_message` tracepoints.

Inbound and outbound traffic is listed for each peer together with information
about the connection. Peers can be selected individually to view recent P2P
messages.

```
$ python3 contrib/tracing/p2p_monitor.py ./src/tapyrusd
```

Lists selectable peers and traffic and connection information.
```
 P2P Message Monitor
 Navigate with UP/DOWN or J/K and select a peer with ENTER or SPACE to see individual P2P messages

 PEER  OUTBOUND              INBOUND               TYPE                   ADDR
    0  46          398 byte  61      1407590 byte  Out-----M--       XX.XX.XXX.196:8333
   11  1156     253570 byte  3431    2394924 byte  Out-----M-Tx      XXX.X.XX.179:8333
   13  3425    1809620 byte  1236     305458 byte  In------T         XXX.X.X.X:60380
   16  1046     241633 byte  1589    1199220 byte  Out-----M-Tx      4faketorv2pbfu7x.onion:8333
   19  577      181679 byte  390      148951 byte  Out-----M-Tx      kfake4vctorjv2o2.onion:8333
   20  11         1248 byte  13         1283 byte  Out-----M--       [2600:fake:64d9:b10c:4436:aaaa:fe:bb]:8333
   21  11         1248 byte  13         1299 byte  Out-----M--       XX.XXX.X.155:8333
   22  5           103 byte  1           102 byte  Out--F-----       XX.XX.XXX.173:8333
   23  11         1248 byte  12         1255 byte  Out-----M--       XX.XXX.XXX.220:8333
   24  3           103 byte  1           102 byte  Out--F-----       XXX.XXX.XXX.64:8333
…
```

Showing recent P2P messages between our node and a selected peer.

```
    ----------------------------------------------------------------------
    |                PEER 16 (4faketorv2pbfu7x.onion:8333)               |
    | OUR NODE                outbound-full-relay                   PEER |
    |                                           <--- sendcmpct (9 bytes) |
    | inv (37 byte) --->                                                 |
    |                                                <--- ping (8 bytes) |
    | pong (8 byte) --->                                                 |
    | inv (37 byte) --->                                                 |
    |                                               <--- addr (31 bytes) |
    | inv (37 byte) --->                                                 |
    |                                       <--- getheaders (1029 bytes) |
    | headers (1 byte) --->                                              |
    |                                           <--- feefilter (8 bytes) |
    |                                                <--- pong (8 bytes) |
    |                                            <--- headers (82 bytes) |
    |                                            <--- addr (30003 bytes) |
    | inv (1261 byte) --->                                               |
    |                                 …                                  |

```

### log_raw_p2p_msgs.py

A BCC Python script showcasing eBPF and USDT limitations when passing data
larger than about 32kb. Based on the `net:inbound_message` and
`net:outbound_message` tracepoints.

Tapyrus P2P messages can be larger than 32kb (e.g. `tx`, `block`, ...). The
eBPF VM's stack is limited to 512 bytes, and we can't allocate more than about
32kb for a P2P message in the eBPF VM. The **message data is cut off** when the
message is larger than MAX_MSG_DATA_LENGTH (see script). This can be detected
in user-space by comparing the data length to the message length variable. The
message is cut off when the data length is smaller than the message length.
A warning is included with the printed message data.

Data is submitted to user-space (i.e. to this script) via a ring buffer. The
throughput of the ring buffer is limited. Each p2p_message is about 32kb in
size. In- or outbound messages submitted to the ring buffer in rapid
succession fill the ring buffer faster than it can be read. Some messages are
lost. BCC prints: `Possibly lost 2 samples` on lost messages.


```
$ python3 contrib/tracing/log_raw_p2p_msgs.py ./src/tapyrusd
```

```
Logging raw P2P messages.
Messages larger that about 32kb will be cut off!
Some messages might be lost!
 outbound msg 'inv' from peer 4 (outbound-full-relay, XX.XXX.XX.4:8333) with 253 bytes: 0705000000be2245c8f844c9f763748e1a7…
…
Warning: incomplete message (only 32568 out of 53552 bytes)! inbound msg 'tx' from peer 32 (outbound-full-relay, XX.XXX.XXX.43:8333) with 53552 bytes: 020000000001fd3c01939c85ad6756ed9fc…
…
Possibly lost 2 samples
```

### connectblock_benchmark.bt

A `bpftrace` script to benchmark the `ConnectBlock()` function during, for
example, a blockchain re-index. Based on the `validation:block_connected` USDT
tracepoint.

The script takes three positional arguments. The first two arguments, the start,
and end height indicate between which blocks the benchmark should be run. The
third acts as a duration threshold in milliseconds. When the `ConnectBlock()`
function takes longer than the threshold, information about the block, is
printed. For more details, see the header comment in the script.

The following command can be used to benchmark, for example, `ConnectBlock()`
between height 1 and 100  while logging all blocks with minimuun time to connect set to 0.

```
$ bpftrace contrib/tracing/connectblock_benchmark.bt 1 100 0
```

In a different terminal, starting Tapyrus Core with
re-indexing enabled.

```
$ ./src/tapyrusd -reindex
```

This produces the following output.
```
Attaching 5 probes...
ConnectBlock benchmark between height 1 and 100 inclusive
Starting Connect Block Benchmark between height 1 and 100.
Starting Connect Block Benchmark between height 1 and 100.
BENCH   62 blk/s     74 tx/s      74 inputs/s       62 sigops/s (height 30)
BENCH   10 blk/s     10 tx/s      10 inputs/s        0 sigops/s (height 31)
Starting Connect Block Benchmark between height 1 and 100.
BENCH   37 blk/s     41 tx/s      41 inputs/s       28 sigops/s (height 25)
Starting Connect Block Benchmark between height 1 and 100.
BENCH   48 blk/s     53 tx/s      53 inputs/s       30 sigops/s (height 37)
BENCH   15 blk/s     15 tx/s      15 inputs/s        9 sigops/s (height 42)
Starting Connect Block Benchmark between height 1 and 100.
BENCH   42 blk/s     47 tx/s      47 inputs/s       31 sigops/s (height 42)
Starting Connect Block Benchmark between height 1 and 100.
BENCH   42 blk/s     47 tx/s      47 inputs/s       31 sigops/s (height 42)
Starting Connect Block Benchmark between height 1 and 100.
BENCH   42 blk/s     47 tx/s      47 inputs/s       31 sigops/s (height 42)
BENCH    3 blk/s      3 tx/s       3 inputs/s        0 sigops/s (height 43)
BENCH   39 blk/s     39 tx/s      39 inputs/s       15 sigops/s (height 56)
Starting Connect Block Benchmark between height 1 and 100.
BENCH   50 blk/s     55 tx/s      55 inputs/s       31 sigops/s (height 50)
BENCH    6 blk/s      6 tx/s       6 inputs/s        5 sigops/s (height 56)
...

Took 16483 ms to connect the blocks between height 1 and 100.

Histogram of block connection times in milliseconds (ms).
@durations: 
[0]                  397 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|



```

### log_utxocache_flush.py

A BCC Python script to log the UTXO cache flushes. Based on the
`utxocache:utxocache_flush` tracepoint.

```bash
$ python3 contrib/tracing/log_utxocache_flush.py ./src/tapyrusd
```

```
Logging utxocache flushes. Ctrl-C to end...
Duration (µs)   Mode       Coins Count     Memory Usage    Prune
730451          IF_NEEDED  22990           3323.54 kB      True
637657          ALWAYS     122320          17124.80 kB     False
81349           ALWAYS     0               1383.49 kB      False
```

### log_utxos.bt

A `bpftrace` script to log information about the coins that are added, spent, or
uncached from the UTXO set. Based on the `utxocache:utxocache_add`, `utxocache:utxocache_spend` and
`utxocache:utxocache_uncache` tracepoints.

```bash
$ bpftrace contrib/tracing/log_utxos.bt
```

This should produce an output similar to the following. If you see bpftrace
warnings like `Lost 24 events`, the eBPF perf ring-buffer is filled faster
than it is being read. You can increase the ring-buffer size by setting the
ENV variable `BPFTRACE_PERF_RB_PAGES` (default 64) at a cost of higher
memory usage. See the [bpftrace reference guide] for more information.

[bpftrace reference guide]: https://github.com/iovisor/bpftrace/blob/master/docs/reference_guide.md#98-bpftrace_perf_rb_pages

```bash
Attaching 4 probes...
OP        Outpoint                                                                Token            Value        Height Coinbase
Added   : 409b414378be14dfd03daed6d390ffd27b4e122f1866009536d52331860fd4b   0      TPC             5000000000       1 Yes
Added   : 963902039acf07d715f0109c7fdebe5939c350e62d6c93424f7923cd7f79faa   0      TPC             5000000000       2 Yes
Added   : 9e546ec670cabceba8d3b2ccb98fe4f21b5e717d8e7193f60c84d530ad04d83   0      TPC             5000000000       3 Yes

```

### log_coinselection.bt

A `bpftrace` script to log information about the coins that areselected by the wallet for every transaction created in the wallet. Based on the `coin_selection:coins_requested`, `coin_selection:selected_coins`, 
`coin_selection:change_info` and `coin_selection:fee_info` tracepoints.

```bash
$ bpftrace contrib/tracing/log_coinselection.bt
```

```
Logging Coin selection 
Request [TPC] : value [0]
Request [c1bd82e2fa1915af84781716691a1a1d25ed00543d2c11f457513d995398f5a] : value [100]
Select [TPC]: try to select [0] using [1] coins of total value [20000] algorithm tried is [knapsack]
Select [TPC]: try to select [5160] using [1] coins of total value [20000] algorithm tried is [knapsack]
Fee [TPC] : value [5160] need [5160]
Request [TPC] : value [20000]
Select [TPC]: try to select [20880] using [1] coins of total value [999987620] algorithm tried is [knapsack]
Request [TPC] : value [0]
Request [c156a0931bb87cd802a2a722bfe329fe986066c979cae3ecbc73a0e4b423843] : value [100]
Select [TPC]: try to select [0] using [1] coins of total value [20000] algorithm tried is [knapsack]
Select [TPC]: try to select [5160] using [1] coins of total value [20000] algorithm tried is [knapsack]
Fee [TPC] : value [5160] need [5160]
Request [TPC] : value [0]
Request [c156a0931bb87cd802a2a722bfe329fe986066c979cae3ecbc73a0e4b423843] : value [50]
Select [TPC]: try to select [0] using [0] coins of total value [0] algorithm tried is [knapsack]
Select [TPC]: try to select [1580] using [1] coins of total value [999990300] algorithm tried is [knapsack]
Select [c156a0931bb87cd802a2a722bfe329fe986066c979cae3ecbc73a0e4b423843]: try to select [50] using [1] coins of total value [100] algorithm tried is [knapsack]
Change [c156a0931bb87cd802a2a722bfe329fe986066c979cae3ecbc73a0e4b423843] : value [50]
```



### mempool_monitor.py

A BCC Python script producing mempool statistics and an event log. Based on the
`mempool:added`, `mempool:removed`, `mempool:replaced`, and `mempool:rejected`
tracepoints.

Statistics include incidence and rate for each event type since the script was
started (`total`) as well as during the last minute (`1 min`) and ten minutes
(`10 min`). The event log shows mempool events in real time, each entry
comprising a timestamp along with all event data available via the event's
tracepoint.

```console
$ python3 contrib/tracing/mempool_monitor.py ./src/tapyrusd
```

```
 Mempool Monitor
 Press CTRL-C to stop.

 ┌─Event count───────────────────────┐  ┌─Event rate─────────────────────────────┐
 │ Event       total   1 min  10 min │  │ Event        total     1 min    10 min │
 │ added        77tx    77tx    77tx │  │ added      3.3tx/s   3.3tx/s   3.3tx/s │
 │ removed      73tx    73tx    73tx │  │ removed    3.1tx/s   3.1tx/s   3.1tx/s │
 │ replaced     10tx    10tx    10tx │  │ replaced   0.4tx/s   0.4tx/s   0.4tx/s │
 │ rejected      0tx     0tx     0tx │  │ rejected   0.0tx/s   0.0tx/s   0.0tx/s │
 └───────────────────────────────────┘  └────────────────────────────────────────┘

 ┌─Event log──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
 │ 12:12:19Z added 654ad6849b0354127c28c30a334eaadc0e43b209cb0c7ab89ed33fcecf3011d4 with feerate 20.00 sat/vB (4500 sat, 225 vbytes)                                          │
 │ 12:12:19Z removed 061ed887bb2b78f53a6b3af8254f73b80ef68c53aa660e6aed25fb7dc60ff68a with feerate 4.44 sat/vB (1000 sat, 225 vbytes) received 0.2 seconds ago: replaced      │
 │ 12:12:19Z removed 654ad6849b0354127c28c30a334eaadc0e43b209cb0c7ab89ed33fcecf3011d4 with feerate 20.00 sat/vB (4500 sat, 225 vbytes) received 0.2 seconds ago: conflict     │
 │ 12:12:19Z added 061ed887bb2b78f53a6b3af8254f73b80ef68c53aa660e6aed25fb7dc60ff68a with feerate 4.44 sat/vB (1000 sat, 225 vbytes)                                           │
 │ 12:12:19Z removed 59c97113cf688faf52655ae256a14ceb62b7e32874eb7c1d332cfbcd4a9bb497 with feerate 295.86 sat/vB (100000 sat, 338 vbytes) received 1.3 seconds ago: block     │
 │ 12:12:19Z removed 2e0c5acee9e98e3494c4068828c8dccae379f37e819d72f21411dda599ebdf2b with feerate 261.78 sat/vB (50000 sat, 191 vbytes) received 1.3 seconds ago: block      │
 │ 12:12:19Z removed 8d7f39b40e501aa8cf2f66de7e3106ac3d0054d068b0bb49b5cd2c55bee020ea with feerate 261.78 sat/vB (50000 sat, 191 vbytes) received 1.3 seconds ago: block      │
 │ 12:12:19Z removed 284ed4e3f90d69dee0fe8dc33a94733da1bb3d5349214652cc757a14400c77f5 with feerate 4.44 sat/vB (1000 sat, 225 vbytes) received 1.3 seconds ago: block         │
 │ 12:12:19Z removed d475a4bca28875cfb23fc37369769b14287db821c5e8b2c9090ed787abb24fc2 with feerate 157.07 sat/vB (30000 sat, 191 vbytes) received 1.3 seconds ago: block      │
 │ 12:12:19Z removed 49c3a44e4ac6e8ca818591a36c6b6eb42dccb303d1487dac05d3b7a6a9ac8329 with feerate 44.44 sat/vB (10000 sat, 225 vbytes) received 0.3 seconds ago: block       │
 │ 12:12:19Z removed 1b90139a711ad0b596e2f5060ede2241e465de556b7edc17d2494f6929c33ed9 with feerate 25.00 sat/vB (5625 sat, 225 vbytes) received 0.3 seconds ago: block        │
 │ 12:12:19Z removed 50d99394d193c007d4054f915c2086ae0ec1bc169b1ba87dbbd3a18782c5203f with feerate 20.00 sat/vB (4500 sat, 225 vbytes) received 3.3 seconds ago: block        │
 │ 12:12:19Z removed 98c4fcd7fd1a90f795179469d8ff2e413200fe35daa2a44be0b1f7014f7eb5a1 with feerate 20.00 sat/vB (4500 sat, 225 vbytes) received 0.3 seconds ago: block        │
 │ 12:12:19Z removed aa919229a6bd58a000dc63cf6810dd368a1c0f0ab27ce17b6bf3a442704598b0 with feerate 13.33 sat/vB (3000 sat, 225 vbytes) received 0.3 seconds ago: block        │
 │ 12:12:19Z removed b753dcd297312f78c87630c5043a43775e013dd0d8069588b695d189637c8307 with feerate 4.46 sat/vB (1000 sat, 224 vbytes) received 1.3 seconds ago: block         │
 │ 12:12:19Z removed 061ed887bb2b78f53a6b3af8254f73b80ef68c53aa660e6aed25fb7dc60ff68a with feerate 4.44 sat/vB (1000 sat, 225 vbytes) received 0.3 seconds ago: block         │
 │ 12:12:19Z added fac2cb1a0b8960c09748d413491cfedc9c9c103b88fb9f4d304b189358cc55ac with feerate 20.00 sat/vB (10380 sat, 519 vbytes)                                         │
 │ 12:12:19Z removed fac2cb1a0b8960c09748d413491cfedc9c9c103b88fb9f4d304b189358cc55ac with feerate 20.00 sat/vB (10380 sat, 519 vbytes) received 0.3 seconds ago: replaced    │
 │ 12:12:19Z replaced fac2cb1a0b8960c09748d413491cfedc9c9c103b88fb9f4d304b189358cc55ac received 0.3 seconds ago (12975 sat, 519 vbytes) with 6a7ee859cf04ffc864512359099e5982f│
 │ 3d5c5750139d0dc5658bfa69085cae8 (10380 sat, 518 vbytes)                                                                                                                    │
 │ 12:12:19Z added 6a7ee859cf04ffc864512359099e5982f3d5c5750139d0dc5658bfa69085cae8 with feerate 25.05 sat/vB (12975 sat, 518 vbytes)                                         │
 │ 12:12:19Z added b2b33fd55c20e68f1a806a7c7a557001690837200d5f6a30098ea825927daac1 with feerate 4.44 sat/vB (1000 sat, 225 vbytes)
 ```