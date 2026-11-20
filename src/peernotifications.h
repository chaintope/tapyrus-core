// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_PEERNOTIFICATIONS_H
#define TAPYRUS_PEERNOTIFICATIONS_H

#include <notificationsinterface.h>

#include <chrono>

/** What the peer-to-peer side publishes. */
class PeerNotifications : public virtual NotificationsInterface
{
public:
    /** The outbound peers' median clock offset from ours is more than threshold. */
    virtual void ReportClockOutOfSync(std::chrono::minutes threshold) = 0;
    virtual void ReportClockInSync() = 0;
};

#endif // TAPYRUS_PEERNOTIFICATIONS_H
