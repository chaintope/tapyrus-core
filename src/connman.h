// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CONNMAN_H
#define TAPYRUS_CONNMAN_H

#include <addrdb.h>
#include <addrman.h>
#include <compat.h>
#include <net.h>
#include <netaddress.h>
#include <neteventsinterface.h>
#include <netmessage.h>
#include <netnode.h>
#include <protocol.h>
#include <random.h>
#include <sync.h>
#include <threadinterrupt.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class CClientUIInterface;
class CScheduler;

struct AddedNodeInfo
{
    std::string strAddedNode;
    CService resolvedAddress;
    bool fConnected;
    bool fInbound;
};

class CConnman
{
public:

    enum NumConnections {
        CONNECTIONS_NONE = 0,
        CONNECTIONS_IN = (1U << 0),
        CONNECTIONS_OUT = (1U << 1),
        CONNECTIONS_ALL = (CONNECTIONS_IN | CONNECTIONS_OUT),
    };

    struct Options
    {
        ServiceFlags nLocalServices = NODE_NONE;
        int nMaxConnections = 0;
        int nMaxOutbound = 0;
        int nMaxAddnode = 0;
        int nMaxFeeler = 0;
        int nBestHeight = 0;
        CClientUIInterface* uiInterface = nullptr;
        NetEventsInterface* m_msgproc = nullptr;
        unsigned int nSendBufferMaxSize = 0;
        unsigned int nReceiveFloodSize = 0;
        uint64_t nMaxOutboundTimeframe = 0;
        uint64_t nMaxOutboundLimit = 0;
        std::vector<std::string> vSeedNodes;
        std::vector<CSubNet> vWhitelistedRange;
        std::vector<CService> vBinds, vWhiteBinds;
        bool m_use_addrman_outgoing = true;
        std::vector<std::string> m_specified_outgoing;
        std::vector<std::string> m_added_nodes;
    };

    void Init(const Options& connOptions) {
        nLocalServices = connOptions.nLocalServices;
        nMaxConnections = connOptions.nMaxConnections;
        nMaxOutbound = std::min(connOptions.nMaxOutbound, connOptions.nMaxConnections);
        nMaxAddnode = connOptions.nMaxAddnode;
        nMaxFeeler = connOptions.nMaxFeeler;
        nBestHeight = connOptions.nBestHeight;
        clientInterface = connOptions.uiInterface;
        m_msgproc = connOptions.m_msgproc;
        nSendBufferMaxSize = connOptions.nSendBufferMaxSize;
        nReceiveFloodSize = connOptions.nReceiveFloodSize;
        {
            LOCK(cs_totalBytesSent);
            nMaxOutboundTimeframe = connOptions.nMaxOutboundTimeframe;
            nMaxOutboundLimit = connOptions.nMaxOutboundLimit;
        }
        vWhitelistedRange = connOptions.vWhitelistedRange;
        {
            LOCK(cs_vAddedNodes);
            vAddedNodes = connOptions.m_added_nodes;
        }
    }

    CConnman(uint64_t seed0, uint64_t seed1);
    ~CConnman();
    bool Start(CScheduler& scheduler, const Options& options);
    void Stop();
    void Interrupt();
    bool GetNetworkActive() const { return fNetworkActive; };
    void SetNetworkActive(bool active);
    void OpenNetworkConnection(const CAddress& addrConnect, bool fCountFailure, CSemaphoreGrant *grantOutbound = nullptr, const char *strDest = nullptr, bool fOneShot = false, bool fFeeler = false, bool manual_connection = false);
    bool CheckIncomingNonce(uint64_t nonce);

    bool ForNode(NodeId id, std::function<bool(CNode* pnode)> func);

    void PushMessage(CNode* pnode, CSerializedNetMsg&& msg);

    template<typename Callable>
    void ForEachNode(Callable&& func)
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes) {
            if (NodeFullyConnected(node))
                func(node);
        }
    };

    template<typename Callable>
    void ForEachNode(Callable&& func) const
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes) {
            if (NodeFullyConnected(node))
                func(node);
        }
    };

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable&& pre, CallableAfter&& post)
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes) {
            if (NodeFullyConnected(node))
                pre(node);
        }
        post();
    };

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable&& pre, CallableAfter&& post) const
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes) {
            if (NodeFullyConnected(node))
                pre(node);
        }
        post();
    };

    // Addrman functions
    size_t GetAddressCount() const;
    void SetServices(const CService &addr, ServiceFlags nServices);
    void MarkAddressGood(const CAddress& addr);
    void AddNewAddresses(const std::vector<CAddress>& vAddr, const CAddress& addrFrom, int64_t nTimePenalty = 0);
    std::vector<CAddress> GetAddresses();

    // Banning is set manually (setban RPC): a banned address or subnet never
    // connects in either direction, and bans are saved to banlist.dat.
    // Discouragement marks a misbehaving peer's address (see Misbehaving() in
    // net_processing.cpp): it may still connect inbound while slots are free,
    // is evicted first, and gets no outbound connection. Discouraged addresses
    // live in a fixed-size rolling filter, so they cannot grow without bound;
    // they are not saved, listed or removable.
    // Neither stops a determined attacker, who can reconnect from another
    // address. Disconnecting a peer for data that a later version may accept
    // can split the network, so punish only what is invalid for good.
    void Ban(const CNetAddr& netAddr, const BanReason& reason, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    void Ban(const CSubNet& subNet, const BanReason& reason, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    // Clears the ban list only (clearbanned RPC); discouraged addresses are not removable.
    void ClearBanned();
    bool IsBanned(CNetAddr ip);
    bool IsBanned(CSubNet subnet);
    void Discourage(const CNetAddr& ip);
    bool IsDiscouraged(const CNetAddr& ip);
    bool Unban(const CNetAddr &ip);
    bool Unban(const CSubNet &ip);
    void GetBanned(banmap_t &banmap);
    void SetBanned(const banmap_t &banmap);

    // This allows temporarily exceeding nMaxOutbound, with the goal of finding
    // a peer that is better than all our current peers.
    void SetTryNewOutboundPeer(bool flag);
    bool GetTryNewOutboundPeer();

    // Return the number of outbound peers we have in excess of our target (eg,
    // if we previously called SetTryNewOutboundPeer(true), and have since set
    // to false, we may have extra peers that we wish to disconnect). This may
    // return a value less than (num_outbound_connections - num_outbound_slots)
    // in cases where some outbound connections are not yet fully connected, or
    // not yet fully disconnected.
    int GetExtraOutboundCount();

    bool AddNode(const std::string& node);
    bool RemoveAddedNode(const std::string& node);
    std::vector<AddedNodeInfo> GetAddedNodeInfo();

    size_t GetNodeCount(NumConnections num);
    void GetNodeStats(std::vector<CNodeStats>& vstats);
    bool DisconnectNode(const std::string& node);
    bool DisconnectNode(NodeId id);

    ServiceFlags GetLocalServices() const;

    //!set the max outbound target in bytes
    void SetMaxOutboundTarget(uint64_t limit);
    uint64_t GetMaxOutboundTarget();

    //!set the timeframe for the max outbound target
    void SetMaxOutboundTimeframe(uint64_t timeframe);
    uint64_t GetMaxOutboundTimeframe();

    //!check if the outbound target is reached
    // if param historicalBlockServingLimit is set true, the function will
    // response true if the limit for serving historical blocks has been reached
    bool OutboundTargetReached(bool historicalBlockServingLimit);

    //!response the bytes left in the current max outbound cycle
    // in case of no limit, it will always response 0
    uint64_t GetOutboundTargetBytesLeft();

    //!response the time in second left in the current max outbound cycle
    // in case of no limit, it will always response 0
    uint64_t GetMaxOutboundTimeLeftInCycle();

    uint64_t GetTotalBytesRecv();
    uint64_t GetTotalBytesSent();

    void SetBestHeight(int height);
    int GetBestHeight() const;

    /** Get a unique deterministic randomizer. */
    CSipHasher GetDeterministicRandomizer(uint64_t id) const;

    unsigned int GetReceiveFloodSize() const;

    void WakeMessageHandler();

    /** Attempts to obfuscate tx time through exponentially distributed emitting.
        Works assuming that a single interval is used.
        Variable intervals will result in privacy decrease.
    */
    int64_t PoissonNextSendInbound(int64_t now, int average_interval_seconds);

private:
    struct ListenSocket {
        SOCKET socket;
        bool whitelisted;

        ListenSocket(SOCKET socket_, bool whitelisted_) : socket(socket_), whitelisted(whitelisted_) {}
    };

    bool BindListenPort(const CService &bindAddr, std::string& strError, bool fWhitelisted = false);
    bool Bind(const CService &addr, unsigned int flags);
    bool InitBinds(const std::vector<CService>& binds, const std::vector<CService>& whiteBinds);
    void ThreadOpenAddedConnections();
    void AddOneShot(const std::string& strDest);
    void ProcessOneShot();
    void ThreadOpenConnections(std::vector<std::string> connect);
    void ThreadMessageHandler();
    void AcceptConnection(const ListenSocket& hListenSocket);
    void ThreadSocketHandler();
    void ThreadDNSAddressSeed();

    uint64_t CalculateKeyedNetGroup(const CAddress& ad) const;

    CNode* FindNode(const CNetAddr& ip);
    CNode* FindNode(const CSubNet& subNet);
    CNode* FindNode(const std::string& addrName);
    CNode* FindNode(const CService& addr);

    bool AttemptToEvictConnection();
    CNode* ConnectNode(CAddress addrConnect, const char *pszDest, bool fCountFailure, bool manual_connection);
    bool IsWhitelistedRange(const CNetAddr &addr);

    void DeleteNode(CNode* pnode);

    NodeId GetNewNodeId();

    size_t SocketSendData(CNode *pnode) const;
    //!check is the banlist has unwritten changes
    bool BannedSetIsDirty();
    //!set the "dirty" flag for the banlist
    void SetBannedSetDirty(bool dirty=true);
    //!clean unused entries (if bantime has expired)
    void SweepBanned();
    void DumpAddresses();
    void DumpData();
    void DumpBanlist();

    // Network stats
    void RecordBytesRecv(uint64_t bytes);
    void RecordBytesSent(uint64_t bytes);

    // Whether the node should be passed out in ForEach* callbacks
    static bool NodeFullyConnected(const CNode* pnode);

    // Network usage totals
    RecursiveMutex cs_totalBytesRecv;
    RecursiveMutex cs_totalBytesSent;
    uint64_t nTotalBytesRecv GUARDED_BY(cs_totalBytesRecv);
    uint64_t nTotalBytesSent GUARDED_BY(cs_totalBytesSent);

    // outbound limit & stats
    uint64_t nMaxOutboundTotalBytesSentInCycle GUARDED_BY(cs_totalBytesSent);
    uint64_t nMaxOutboundCycleStartTime GUARDED_BY(cs_totalBytesSent);
    uint64_t nMaxOutboundLimit GUARDED_BY(cs_totalBytesSent);
    uint64_t nMaxOutboundTimeframe GUARDED_BY(cs_totalBytesSent);

    // Whitelisted ranges. Any node connecting from these is automatically
    // whitelisted (as well as those connecting to whitelisted binds).
    std::vector<CSubNet> vWhitelistedRange;

    unsigned int nSendBufferMaxSize;
    unsigned int nReceiveFloodSize;

    std::vector<ListenSocket> vhListenSocket;
    std::atomic<bool> fNetworkActive;
    banmap_t setBanned;
    RecursiveMutex cs_setBanned;
    CRollingBloomFilter m_discouraged GUARDED_BY(cs_setBanned){50000, 0.000001};
    bool setBannedIsDirty;
    bool fAddressesInitialized;
    CAddrMan addrman;
    std::deque<std::string> vOneShots;
    RecursiveMutex cs_vOneShots;
    std::vector<std::string> vAddedNodes GUARDED_BY(cs_vAddedNodes);
    RecursiveMutex cs_vAddedNodes;
    std::vector<CNode*> vNodes;
    std::list<CNode*> vNodesDisconnected;
    mutable RecursiveMutex cs_vNodes;
    std::atomic<NodeId> nLastNodeId;

    /** Services this instance offers */
    ServiceFlags nLocalServices;

    std::unique_ptr<CSemaphore> semOutbound;
    std::unique_ptr<CSemaphore> semAddnode;
    int nMaxConnections;
    int nMaxOutbound;
    int nMaxAddnode;
    int nMaxFeeler;
    std::atomic<int> nBestHeight;
    CClientUIInterface* clientInterface;
    NetEventsInterface* m_msgproc;

    /** SipHasher seeds for deterministic randomness */
    const uint64_t nSeed0, nSeed1;

    /** flag for waking the message processor. */
    bool fMsgProcWake;

    std::condition_variable condMsgProc;
    Mutex mutexMsgProc;
    std::atomic<bool> flagInterruptMsgProc;

    CThreadInterrupt interruptNet;

    std::thread threadDNSAddressSeed;
    std::thread threadSocketHandler;
    std::thread threadOpenAddedConnections;
    std::thread threadOpenConnections;
    std::thread threadMessageHandler;

    /** flag for deciding to connect to an extra outbound peer,
     *  in excess of nMaxOutbound
     *  This takes the place of a feeler connection */
    std::atomic_bool m_try_another_outbound_peer;

    std::atomic<int64_t> m_next_send_inv_to_incoming{0};

    friend struct CConnmanTest;
};
extern std::unique_ptr<CConnman> g_connman;

struct CombinerAll
{
    typedef bool result_type;

    template<typename I>
    bool operator()(I first, I last) const
    {
        while (first != last) {
            if (!(*first)) return false;
            ++first;
        }
        return true;
    }
};

#endif // TAPYRUS_CONNMAN_H
