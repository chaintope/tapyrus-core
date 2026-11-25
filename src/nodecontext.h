// Copyright (c) 2019 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_NODECONTEXT_H
#define TAPYRUS_NODECONTEXT_H

#include <notificationhub.h>

#include <atomic>
#include <memory>

class CBlockPolicyEstimator;
class CacheSettings;
class CConnman;
class CTxMemPool;
class MempoolFeeListener;
class PeerManager;
struct PolicySettings;

/** The objects a running node owns, held in one place instead of globals.
 *  tapyrusd and tapyrus-qt create one and pass it to init, which fills it;
 *  RPC reaches it through the request context and the interfaces hold a
 *  reference. The pointer members are null until init creates them. */
struct NodeContext {
    //! First, so the objects below that read it are destroyed before it.
    std::unique_ptr<const PolicySettings> policy;
    //! What the components publish to; keeps the warnings the GUI and RPC read.
    //! Declared before peerman, which holds a reference to it. The block files,
    //! the coins views and the indexes refer to it too, so Shutdown() releases
    //! them before this is destroyed.
    NotificationHub notifications;
    std::unique_ptr<const CacheSettings> cache;
    std::unique_ptr<CBlockPolicyEstimator> fee_estimator;
    //! Before peerman, which holds a reference to it. The chainstate refers to it
    //! too, so Shutdown() releases the chainstate before this is destroyed.
    std::unique_ptr<CTxMemPool> mempool;
    //! Feeds fee_estimator from mempool. After both, so it is destroyed first.
    std::unique_ptr<MempoolFeeListener> mempool_fee_listener;
    //! Set once the mempool has been loaded from disk (or loading was skipped);
    //! until then it is not dumped at shutdown and savemempool refuses.
    std::atomic_bool mempool_loaded{false};
    std::unique_ptr<CConnman> connman;
    std::unique_ptr<PeerManager> peerman;

    //! Declared here and defined in the .cpp, so this header needs only
    //! forward declarations of the pointer members' types.
    NodeContext();
    ~NodeContext();
};

#endif // TAPYRUS_NODECONTEXT_H
