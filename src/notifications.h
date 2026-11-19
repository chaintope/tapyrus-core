// Copyright (c) 2023 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_NOTIFICATIONS_H
#define TAPYRUS_NOTIFICATIONS_H

#include <uint256.h>

#include <chrono>
#include <string>

/** Why the node shuts down. All but STOP_HEIGHT_REACHED are fatal errors. */
enum class ShutdownReason {
    // The chain
    DISK_SPACE_LOW,
    BLOCK_WRITE_FAILED,
    UNDO_WRITE_FAILED,
    BLOCK_FILE_FLUSH_FAILED,
    BLOCK_INDEX_WRITE_FAILED,
    COINS_WRITE_FAILED,
    COINS_READ_FAILED,
    CORRUPT_BLOCK,
    BLOCK_READ_FAILED,
    SYSTEM_ERROR,
    FLUSH_SYSTEM_ERROR,
    STOP_HEIGHT_REACHED,
    // The indexes
    INDEX_BLOCK_READ_FAILED,
    INDEX_BLOCK_WRITE_FAILED,
    INDEX_FIRST_BLOCK_NOT_GENESIS,
    INDEX_LOCATOR_BLOCK_NOT_FOUND,
    INDEX_INIT_FAILED,
};

/**
 * Status a component publishes for the user to see and act on, as Core's
 * kernel::Notifications. One-way: nothing is returned to the publisher. In the
 * node, the components publish to the node's NotificationHub, which keeps what
 * the GUI and RPC read and passes it on to its subscribers; a component in
 * another process would publish through an implementation that forwards it.
 */
class NotificationsInterface
{
public:
    virtual ~NotificationsInterface() = default;

    /** The node shuts down for reason; detail is what the message names, such as a block hash. */
    virtual void ReportShutdown(ShutdownReason reason, const std::string& detail) = 0;
};

/** What the chain classes publish. */
class ChainNotifications : public virtual NotificationsInterface
{
public:
    /** A valid fork with large work branches off our chain after fork_base. */
    virtual void ReportLargeWorkFork(const uint256& fork_base) = 0;
    /** An invalid chain at least 6 blocks longer than ours exists. */
    virtual void ReportInvalidChainAhead() = 0;
    /** There is neither a large-work fork nor a longer invalid chain. */
    virtual void ReportForkResolved() = 0;
};
/** What the peer-to-peer side publishes. */
class PeerNotifications : public virtual NotificationsInterface
{
public:
    /** The outbound peers' median clock offset from ours is more than threshold. */
    virtual void ReportClockOutOfSync(std::chrono::minutes threshold) = 0;
    virtual void ReportClockInSync() = 0;
};
/** What the indexes publish. */
class IndexNotifications : public virtual NotificationsInterface
{
public:
    /** Moving the txindex out of the block tree database is percent done; 100 when finished. */
    virtual void ReportUpgradeProgress(int percent) = 0;
};

/** Everything the components publish, each call doing nothing unless overridden:
 *  what the node's NotificationHub and its subscribers implement. */
class Notifications : public ChainNotifications, public PeerNotifications, public IndexNotifications
{
public:
    void ReportShutdown(ShutdownReason reason, const std::string& detail) override {}
    void ReportClockOutOfSync(std::chrono::minutes threshold) override {}
    void ReportClockInSync() override {}
    void ReportLargeWorkFork(const uint256& fork_base) override {}
    void ReportInvalidChainAhead() override {}
    void ReportForkResolved() override {}
    void ReportUpgradeProgress(int percent) override {}
};

#endif // TAPYRUS_NOTIFICATIONS_H
