// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_INDEX_INDEXNOTIFICATIONS_H
#define TAPYRUS_INDEX_INDEXNOTIFICATIONS_H

#include <notificationsinterface.h>

/** What the indexes publish. */
class IndexNotifications : public virtual NotificationsInterface
{
public:
    /** Moving the txindex out of the block tree database is percent done; 100 when finished. */
    virtual void ReportUpgradeProgress(int percent) = 0;
};

#endif // TAPYRUS_INDEX_INDEXNOTIFICATIONS_H
