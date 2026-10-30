// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_NODESTATE_H
#define TAPYRUS_NODESTATE_H

#include <bloom.h>
#include <netaddress.h>
#include <partiallydownloadedblock.h>
#include <protocol.h>
#include <sync.h>
#include <uint256.h>
#include <utiltime.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

class CBlockIndex;

/** Blocks that are in flight, and that are in the queue to be downloaded. */
struct QueuedBlock {
    uint256 hash;
    const CBlockIndex* pindex;                               //!< Optional.
    bool fValidatedHeaders;                                  //!< Whether this block has validated headers at the time of request.
    std::unique_ptr<PartiallyDownloadedBlock> partialBlock;  //!< Optional, used for CMPCTBLOCK downloads
};

struct CBlockReject {
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};

/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The peer's address
    const CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;

    /** Protects misbehavior data members */
    Mutex m_misbehavior_mutex;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior GUARDED_BY(m_misbehavior_mutex){0};
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool m_should_discourage GUARDED_BY(m_misbehavior_mutex){false};
    //! String name of this peer (debugging/logging purposes).
    const std::string name;
    //! List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    const CBlockIndex *pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    const CBlockIndex *pindexLastCommonBlock;
    //! The best header we have sent our peer.
    const CBlockIndex *pindexBestHeaderSent;
    //! Length of current-streak of unconnecting headers announcements
    int nUnconnectingHeaders;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! When to potentially disconnect peer for stalling headers download
    int64_t nHeadersSyncTimeout;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    std::list<QueuedBlock> vBlocksInFlight;
    //! When the first entry in vBlocksInFlight started downloading. Don't care when vBlocksInFlight is empty.
    int64_t nDownloadingSince;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;
    //! Whether this peer wants invs or headers (when possible) for block announcements.
    bool fPreferHeaders;
    //! Whether this peer wants invs or cmpctblocks (when possible) for block announcements.
    bool fPreferHeaderAndIDs;
    /**
      * Whether this peer has told us (via SENDCMPCT) that it will send us
      * cmpctblocks/blocktxns using the compact block version we use. Tapyrus
      * has only ever had one compact block version, so this is a single flag
      * rather than a separate "supports our desired version" check.
      */
    bool fProvidesHeaderAndIDs;

    //! The number of most recently announced transactions a peer can request.
    static constexpr uint32_t INVENTORY_MAX_RECENT_RELAY = 3500;
    //! A rolling bloom filter of all announced tx CInvs to this peer.
    CRollingBloomFilter m_recently_announced_invs = CRollingBloomFilter{INVENTORY_MAX_RECENT_RELAY, 0.000001};

    /** State used to enforce CHAIN_SYNC_TIMEOUT
      * Only in effect for outbound, non-manual connections, with
      * m_protect == false
      * Algorithm: if a peer's best known block has less work than our tip,
      * set a timeout CHAIN_SYNC_TIMEOUT seconds in the future:
      *   - If at timeout their best known block now has more work than our tip
      *     when the timeout was set, then either reset the timeout or clear it
      *     (after comparing against our current tip's work)
      *   - If at timeout their best known block still has less work than our
      *     tip did when the timeout was set, then send a getheaders message,
      *     and set a shorter timeout, HEADERS_RESPONSE_TIME seconds in future.
      *     If their best known block is still behind when that new timeout is
      *     reached, disconnect.
      */
    struct ChainSyncTimeoutState {
        //! A timeout used for checking whether our peer has sufficiently synced
        int64_t m_timeout;
        //! A header with the work we require on our peer's chain
        const CBlockIndex * m_work_header;
        //! After timeout is reached, set to true after sending getheaders
        bool m_sent_getheaders;
        //! Whether this peer is protected from disconnection due to a bad/slow chain
        bool m_protect;
    };

    ChainSyncTimeoutState m_chain_sync;

    //! Time of last new block announcement
    int64_t m_last_block_announcement;

    /** Number of addresses that can be processed from this peer. Start at 1 to
     *  permit self-announcement. Guarded by cs_main. */
    double m_addr_token_bucket{1.0};
    /** When m_addr_token_bucket was last updated. Guarded by cs_main. */
    std::chrono::microseconds m_addr_token_timestamp;
    /** Total number of addresses dropped due to rate limiting. Guarded by cs_main. */
    uint64_t m_addr_rate_limited{0};
    /** Total number of addresses processed (excludes rate-limited ones). Guarded by cs_main. */
    uint64_t m_addr_processed{0};

    CNodeState(CAddress addrIn, std::string addrNameIn) : address(addrIn), name(addrNameIn) {
        fCurrentlyConnected = false;
        nMisbehavior = 0;
        m_should_discourage = false;
        pindexBestKnownBlock = nullptr;
        hashLastUnknownBlock.SetNull();
        pindexLastCommonBlock = nullptr;
        pindexBestHeaderSent = nullptr;
        nUnconnectingHeaders = 0;
        fSyncStarted = false;
        nHeadersSyncTimeout = 0;
        nStallingSince = 0;
        nDownloadingSince = 0;
        fPreferredDownload = false;
        fPreferHeaders = false;
        fPreferHeaderAndIDs = false;
        fProvidesHeaderAndIDs = false;
        m_chain_sync = { 0, nullptr, false, false };
        m_last_block_announcement = 0;
        m_recently_announced_invs.reset();
        m_addr_token_bucket = 1.0;
        m_addr_token_timestamp = std::chrono::microseconds(GetTimeMicros(true));
        m_addr_rate_limited = 0;
        m_addr_processed = 0;
    }
};

#endif // TAPYRUS_NODESTATE_H
