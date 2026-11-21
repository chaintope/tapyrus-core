// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CHAINNOTIFICATIONS_H
#define TAPYRUS_CHAINNOTIFICATIONS_H

#include <notificationsinterface.h>
#include <uint256.h>

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
    /** The tip reached height, the height -stopatheight asks the node to stop at. */
    virtual void ReportStopHeightReached(int height) = 0;
};

#endif // TAPYRUS_CHAINNOTIFICATIONS_H
