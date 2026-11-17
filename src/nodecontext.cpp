// Copyright (c) 2019 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <nodecontext.h>

#include <connman.h>
#include <net_processing.h>
#include <policy/policysettings.h>

NodeContext::NodeContext() = default;

NodeContext::~NodeContext()
{
    if (g_policy_settings == policy.get()) g_policy_settings = nullptr;
}
