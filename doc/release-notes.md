(note: this is a temporary file, to be added-to by anybody, and moved to
release-notes at release time)

Notable changes
===============

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
