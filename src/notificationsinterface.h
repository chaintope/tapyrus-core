// Copyright (c) 2023 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_NOTIFICATIONSINTERFACE_H
#define TAPYRUS_NOTIFICATIONSINTERFACE_H

#include <string>

/** An error a component cannot recover from. */
enum class FatalErrorReason {
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

    /** A fatal error; detail is what the message names, such as a block hash. */
    virtual void ReportFatalError(FatalErrorReason reason, const std::string& detail) = 0;
};

#endif // TAPYRUS_NOTIFICATIONSINTERFACE_H
