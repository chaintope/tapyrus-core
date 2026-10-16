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

P2P
---

- When a block arrives, the node now clears outstanding requests for that
  block to every peer, not only the one that sent it. A block completed from
  one peer's compact block used to leave a request to another peer pending,
  and that peer was disconnected for a block download timeout 10 minutes or
  more later.
