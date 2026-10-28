// Copyright (c) 2019 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_NODECONTEXT_H
#define TAPYRUS_NODECONTEXT_H

#include <memory>

class CConnman;
class PeerLogicValidation;
struct PolicySettings;

/** The objects a running node owns, held in one place instead of globals.
 *  tapyrusd and tapyrus-qt create one and pass it to init, which fills it;
 *  RPC reaches it through the request context and the interfaces hold a
 *  reference. Members are null until init creates them. */
struct NodeContext {
    //! First, so the objects below that read it are destroyed before it.
    std::unique_ptr<const PolicySettings> policy;
    std::unique_ptr<CConnman> connman;
    std::unique_ptr<PeerLogicValidation> peer_logic;

    //! Declared here and defined in the .cpp, so this header needs only
    //! forward declarations of the member types.
    NodeContext();
    ~NodeContext();
};

#endif // TAPYRUS_NODECONTEXT_H
