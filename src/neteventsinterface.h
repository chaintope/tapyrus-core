// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_NETEVENTSINTERFACE_H
#define TAPYRUS_NETEVENTSINTERFACE_H

#include <net.h>

#include <atomic>

class CNode;

/**
 * Interface for message handling
 */
class NetEventsInterface
{
public:
    virtual bool ProcessMessages(CNode* pnode, std::atomic<bool>& interrupt) = 0;
    virtual bool SendMessages(CNode* pnode) = 0;
    virtual void InitializeNode(CNode* pnode) = 0;
    virtual void FinalizeNode(NodeId id, bool& update_connection_time) = 0;

protected:
    /**
     * Protected destructor so that instances can only be deleted by derived classes.
     * If that restriction is no longer desired, this should be made public and virtual.
     */
    ~NetEventsInterface() = default;
};

#endif // TAPYRUS_NETEVENTSINTERFACE_H
