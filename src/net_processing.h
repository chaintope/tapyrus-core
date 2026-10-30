// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include <net.h>
#include <connman.h>
#include <netnode.h>
#include <neteventsinterface.h>
#include <validationinterface.h>
#include <cs_main.h>
#include <nodestate.h>
#include <orphantxpool.h>
#include <sync.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/** Maximum number of outstanding CMPCTBLOCK requests for the same block. */
static const unsigned int MAX_CMPCTBLOCKS_INFLIGHT_PER_BLOCK = 3;

class BlockTransactionsRequest;
class CBlock;
class CBlockHeader;
class CBlockHeaderAndShortTxIDs;
class CChainParams;
struct CNodeStateStats;


class PeerManager final : public CValidationInterface, public NetEventsInterface {
private:
    CConnman& m_connman;

public:
    PeerManager(CConnman& connman, CScheduler& scheduler);

    /**
     * Overridden from CValidationInterface.
     */
    void BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexConnected, const std::vector<CTransactionRef>& vtxConflicted) override;
    /**
     * Overridden from CValidationInterface.
     */
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;
    /**
     * Overridden from CValidationInterface.
     */
    void BlockChecked(const CBlock& block, const CValidationState& state) override;
    /**
     * Overridden from CValidationInterface.
     */
    void NewValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &pblock) override;
    /**
     * Overridden from CValidationInterface. Relays the transactions of a
     * successfully-accepted package that are still in the mempool. See policy/packages.cpp and the
     * comment on CValidationInterface::PackageTransactionsRelay().
     */
    void PackageTransactionsRelay(const std::vector<CTransactionRef>& txns) override;

    /** Initialize a peer by adding it to mapNodeState and pushing a message requesting its version */
    void InitializeNode(CNode* pnode) override;
    /** Handle removal of a peer by updating various state and removing it from mapNodeState */
    void FinalizeNode(NodeId nodeid, bool& fUpdateConnectionTime) override;
    /**
    * Process protocol messages received from a given node
    *
    * @param[in]   pfrom           The node which we have received messages from.
    * @param[in]   interrupt       Interrupt condition for processing threads
    */
    bool ProcessMessages(CNode* pfrom, std::atomic<bool>& interrupt) override;
    /**
    * Send queued protocol messages to be sent to a give node.
    *
    * @param[in]   pto             The node which we are sending messages to.
    * @return                      True if there is more work to be done
    */
    bool SendMessages(CNode* pto) override;

    /** Consider evicting an outbound peer based on the amount of time they've been behind our tip */
    void ConsiderEviction(CNode *pto, int64_t time_in_seconds);
    /** Evict extra outbound peers. If we think our tip may be stale, connect to an extra outbound */
    void CheckForStaleTipAndEvictPeers();
    /** If we have extra outbound peers, try to disconnect the one with the oldest block announcement */
    void EvictExtraOutboundPeers(int64_t time_in_seconds);

    /** Get statistics from node state */
    bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);
    /** Increment a peer's misbehavior score; past `-banscore` the peer is disconnected and discouraged. */
    void Misbehaving(NodeId pnode, int howmuch, const std::string& message = "") EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    /** Sets a peer's last block announcement time; used by the stale-tip eviction tests. */
    void UpdateLastBlockAnnounceTime(NodeId node, int64_t time_in_seconds);

private:
    int64_t m_stale_tip_check_time; //! Next time to check for stale tip

    /** Number of nodes with fSyncStarted. */
    int nSyncStarted GUARDED_BY(cs_main) = 0;

    /**
     * Sources of received blocks, saved to be able to send them reject
     * messages or ban them when processing happens afterwards.
     * Set mapBlockSource[hash].second to false if the node should not be
     * punished if the block is invalid.
     */
    std::map<uint256, std::pair<NodeId, bool>> mapBlockSource GUARDED_BY(cs_main);

    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.3 MB
     */
    std::unique_ptr<CRollingBloomFilter> recentRejects GUARDED_BY(cs_main);
    uint256 hashRecentRejectsChainTip GUARDED_BY(cs_main);

    /* Multimap used to preserve insertion order */
    std::multimap<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight GUARDED_BY(cs_main);

    /** Stack of nodes which we have set to announce using compact blocks */
    std::list<NodeId> lNodesAnnouncingHeaderAndIDs GUARDED_BY(cs_main);

    /** Number of preferable block download peers. */
    int nPreferredDownload GUARDED_BY(cs_main) = 0;

    /** Number of peers from which we're downloading blocks. */
    int nPeersWithValidatedDownloads GUARDED_BY(cs_main) = 0;

    /** Number of outbound peers with m_chain_sync.m_protect. */
    int m_outbound_peers_with_protect_from_disconnect GUARDED_BY(cs_main) = 0;

    /** When our tip was last updated. */
    std::atomic<int64_t> m_last_tip_update{0};

    /** Relay map */
    typedef std::map<uint256, CTransactionRef> MapRelay;
    MapRelay mapRelay GUARDED_BY(cs_main);
    /** Expiration-time ordered list of (expire time, relay map entry) pairs. */
    std::deque<std::pair<int64_t, MapRelay::iterator>> vRelayExpiration GUARDED_BY(cs_main);

    std::atomic<int64_t> nTimeBestReceived{0}; // Used only to inform the wallet of when we last received a block

    /** Guards the orphan pool and the compact-block extra-transaction ring.
     *  Taken after cs_main and before mempool.cs. */
    Mutex m_tx_download_mutex;
    OrphanTxPool m_orphan_tx_pool GUARDED_BY(m_tx_download_mutex);

    size_t vExtraTxnForCompactIt GUARDED_BY(m_tx_download_mutex) = 0;
    std::vector<std::pair<uint256, CTransactionRef>> vExtraTxnForCompact GUARDED_BY(m_tx_download_mutex);

    /** Map maintaining per-node state. */
    std::map<NodeId, CNodeState> mapNodeState GUARDED_BY(cs_main);

    // All of the following cache a recent block, and are protected by cs_most_recent_block
    Mutex cs_most_recent_block;
    std::shared_ptr<const CBlock> most_recent_block GUARDED_BY(cs_most_recent_block);
    std::shared_ptr<const CBlockHeaderAndShortTxIDs> most_recent_compact_block GUARDED_BY(cs_most_recent_block);
    uint256 most_recent_block_hash GUARDED_BY(cs_most_recent_block);

    CNodeState* State(NodeId pnode) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void UpdatePreferredDownload(CNode* node, CNodeState* state) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool MarkBlockAsReceived(const uint256& hash, std::optional<NodeId> from_peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const CBlockIndex* pindex = nullptr, std::list<QueuedBlock>::iterator** pit = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void ProcessBlockAvailability(NodeId nodeid) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void UpdateBlockAvailability(NodeId nodeid, const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool TipMayBeStale() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    static bool CanDirectFetch() EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    static bool PeerHasHeader(CNodeState* state, const CBlockIndex* pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<const CBlockIndex*>& vBlocks, NodeId& nodeStaller) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    static bool IsOutboundDisconnectionCandidate(const CNode* node);
    void AddToCompactExtraTransactions(const CTransactionRef& tx) EXCLUSIVE_LOCKS_REQUIRED(m_tx_download_mutex);
    static bool BlockRequestAllowed(const CBlockIndex* pindex);
    bool AlreadyHave(const CInv& inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_tx_download_mutex);
    CTransactionRef FindTxForGetData(const CNode* peer, const uint256& txid, std::chrono::seconds mempool_req, std::chrono::seconds now);

    void PushNodeVersion(CNode* pnode, int64_t nTime);
    void MaybeSetPeerAsAnnouncingHeaderAndIDs(NodeId nodeid);
    void RelayAddress(const CAddress& addr, bool fReachable);
    void ProcessGetBlockData(CNode* pfrom, const CInv& inv);
    void ProcessGetData(CNode* pfrom, const std::atomic<bool>& interruptMsgProc);
    void SendBlockTransactions(const CBlock& block, const BlockTransactionsRequest& req, CNode* pfrom);
    bool ProcessHeadersMessage(CNode* pfrom, const std::vector<CBlockHeader>& headers, const CChainParams& chainparams, bool punish_duplicate_invalid);
    void ProcessOrphanTx(std::set<uint256>& orphan_work_set) EXCLUSIVE_LOCKS_REQUIRED(cs_main, m_tx_download_mutex);
    bool ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, int64_t nTimeReceived, const CChainParams& chainparams, const std::atomic<bool>& interruptMsgProc);
    bool MaybeDiscourageAndDisconnect(CNode* pnode);
};

struct CNodeStateStats {
    int nMisbehavior = 0;
    int nSyncHeight = -1;
    int nCommonHeight = -1;
    std::vector<int> vHeightInFlight;
    uint64_t m_addr_processed = 0;
    uint64_t m_addr_rate_limited = 0;
};

/** Relay a transaction to all our peers*/
void RelayTransaction(const CTransaction& tx, CConnman* connman);

#endif // BITCOIN_NET_PROCESSING_H
