// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ISSUEDCOLORIDS_H
#define BITCOIN_ISSUEDCOLORIDS_H

#include <coloridentifier.h>
#include <cs_main.h>
#include <dbwrapper.h>

#include <set>

/**
 * Single source of truth for NON_REISSUABLE and NFT colorIds issued on-chain.
 *
 * Holds both the confirmed in-memory set and a Changeset that is written
 * atomically with DB_BEST_BLOCK in CCoinsViewDB::BatchWrite.  This prevents
 * a crash between the colorId write and the UTXO flush from leaving the
 * chainstate database in a state where a block cannot be reconnected
 * (bad-txns-colorid-already-issued).
 *
 * Lifecycle (mirrors pcoinsdbview / pcoinsTip):
 *   1. Constructed empty and connected to pcoinsdbview before ReplayBlocks,
 *      so DisconnectBlock inside ReplayBlocks can stage erases that
 *      CommitToBatch writes atomically with the replay's UTXO flush.
 *   2. SetConfirmed() is called after ReplayBlocks to populate the confirmed set.
 *   3. Destroyed on shutdown after pcoinsdbview.
 */
class CIssuedColorIds
{
public:
    CIssuedColorIds() = default;

    /** Populate the confirmed set from ids loaded by the caller.  Call after ReplayBlocks. */
    void SetConfirmed(std::set<ColorIdentifier> ids) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** True if colorId is confirmed on-chain.  All callers hold cs_main. */
    bool IsIssued(const ColorIdentifier& id) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Stage inserts: update confirmed set immediately and queue DB writes.
     * Called from ConnectBlock under cs_main.
     */
    void Insert(const std::set<ColorIdentifier>& ids) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Stage an erase: update confirmed set immediately and queue DB erase.
     * Called from DisconnectBlock under cs_main.
     */
    void Erase(const ColorIdentifier& id) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * Write all staged changes to batch, then clear them.
     * Called from CCoinsViewDB::BatchWrite in the final batch so that
     * colorId writes are atomic with DB_BEST_BLOCK.
     */
    void CommitToBatch(CDBBatch& batch) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Return a copy with the same confirmed set but empty pending changes. */
    std::unique_ptr<CIssuedColorIds> Clone() const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    std::set<ColorIdentifier> m_confirmed GUARDED_BY(cs_main);

    /**
     * Pending inserts and erases managed together: staging an insert
     * cancels any pending erase for the same id, and vice versa.
     * Written to LevelDB only via CommitToBatch.
     */
    struct Changeset {
        std::set<ColorIdentifier> inserts;
        std::set<ColorIdentifier> erases;
        bool empty() const { return inserts.empty() && erases.empty(); }
        void clear()        { inserts.clear(); erases.clear(); }
    } m_pending GUARDED_BY(cs_main);
};

#endif // BITCOIN_ISSUEDCOLORIDS_H
