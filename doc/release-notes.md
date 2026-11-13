(note: this is a temporary file, to be added-to by anybody, and moved to
release-notes at release time)

Notable changes
===============

Block validation
----------------

- Each transaction in a block is now checked against the maximum block size
  in effect at that block's height, as the block itself already was. It used
  to be checked against the latest limit, so after a MAXBLOCKSIZE xfield
  change lowered the limit, a node could reject an earlier block that was
  valid when it was accepted, for example while reindexing.

Updated RPCs
------------

- `gettxoutsetinfo` returns a new field, `issued_colorids_hash`: a hash of the
  issued NON_REISSUABLE and NFT colorIds, which are kept in the chainstate
  database alongside the UTXO set. `hash_serialized_3` is unchanged and still
  covers only the UTXO set, so it stays comparable with older versions. To
  check that two nodes have the same chainstate, compare both fields.

- `submitpackage` results now describe the mempool after the whole package
  has been processed. A transaction that was accepted but then removed by a
  later transaction in the same package used to be reported as accepted. It
  is now rejected with `mempool full` if a later transaction pushed it out of
  a full mempool, or with `replaced-in-package` if a later transaction
  replaced it or one of its mempool parents.

P2P
---

- When a block arrives, the node now clears outstanding requests for that
  block to every peer, not only the one that sent it. A block completed from
  one peer's compact block used to leave a request to another peer pending,
  and that peer was disconnected for a block download timeout 10 minutes or
  more later.

- A peer could run a node out of memory by sending TCP urgent data, which made
  the node keep reading from that peer after its receive queue was full. The
  node now never reads from a peer while its queue is full. All platforms were
  exposed, because the node uses `select()` on every platform. Bitcoin Core
  disclosed the same issue as CVE-2026-117350 for its non-Linux systems.

- Tor inbound peers are no longer whitelisted by `-whitelist` (CVE-2025-62860).
  A Tor peer connects from the Tor daemon's address, so a range covering it,
  such as 127.0.0.1 or a container network, whitelisted every Tor peer: they
  could not be banned, their policy-rejected transactions were relayed, and
  they got inventory without the trickle delay. The node now listens for Tor
  on a separate onion bind, `-bind=<addr>[:<port>]=onion`, by default
  127.0.0.1 at the P2P port + 1 (2358, dev 12384), and never applies
  `-whitelist` to peers arriving there. A hidden service set up by hand in
  torrc should forward to this port; see `doc/tor.md`.

- The node refuses to start when a `-whitelist` range covers an address Tor
  connects from: the `-proxy` or `-onion` address, or, with `-listenonion`,
  127.0.0.1, ::1 or the `-torcontrol` address. Narrow the range, or use
  `-whitebind` for trusted peers. A node that whitelists 127.0.0.1 for local
  software connecting over P2P must now also set `-listenonion=0`, or use
  `-whitebind` instead.

- `-listenonion` now asks Tor for a v3 onion service. It used to ask for v2,
  which Tor 0.4.6 and later reject, so no automatic onion service was
  created on any current Tor. The key is kept in `onion_v3_private_key`, and
  the old `onion_private_key` file is no longer used. A v3 address can't be
  advertised to peers until addrv2 (BIP155) is supported, so the node only
  logs it.

- With `-listenonion`, the node refuses to start when it can't bind the
  address its onion service forwards to, rather than let Tor forward peers
  to a port another program owns. It also refuses an onion bind on the port
  of a `-bind` or `-whitebind` on the same or a wildcard address, and
  `-port=65535` when an onion bind needs the P2P port + 1. It warns when the
  onion service forwards to a clearnet `-bind`, where Tor peers arrive
  untagged, and when every `-bind` is an onion bind.

- `-onion` now reaches v3 `.onion` names given to `-connect` or `-addnode`.
  They used to go only through `-proxy`, so a node with just `-onion` could
  not connect to them. A `.onion` name is never looked up in DNS, and
  `-noonion` now blocks v3 names too.

Downgrading warning
-------------------

- Block and undo file positions in the block index database are now stored
  as 64-bit values. Undo files (`rev*.dat`) have no size limit, and a
  position past 4 GiB used to wrap around and corrupt the undo data. The
  values use a variable-length encoding, so an existing block index is read
  unchanged and no reindex is needed to upgrade.

  This only works in one direction once an undo file has grown past 4 GiB.
  The block index then holds a position that versions without this change
  cannot read, and those versions will refuse to start against it. To move
  such a node back to an older version, start the older version with
  `-reindex`. Undo files normally stay far below 4 GiB, and a node whose
  undo files all do can be downgraded as before.
