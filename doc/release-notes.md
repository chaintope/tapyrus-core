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
