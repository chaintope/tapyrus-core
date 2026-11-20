// Copyright (c) 2019 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <nodecontext.h>

#include <cachesettings.h>
#include <chainstatemanager.h>
#include <connman.h>
#include <net_processing.h>
#include <notificationhub.h>
#include <policy/policysettings.h>

NodeContext::NodeContext() = default;

NodeContext::~NodeContext()
{
    if (g_policy_settings == policy.get()) g_policy_settings = nullptr;
    if (g_chainstate_manager.ReportsTo(notifications)) g_chainstate_manager.SetNotifications(nullptr);
}
