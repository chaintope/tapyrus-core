// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_INTERFACES_CHAIN_H
#define TAPYRUS_INTERFACES_CHAIN_H

#include <amount.h>
#include <primitives/transaction.h>

#include <memory>
#include <stddef.h>

class CFeeRate;
class CValidationState;
struct NodeContext;
class uint256;
class WalletNotifications;
struct FeeCalculation;
enum class RBFTransactionState;

namespace interfaces {

/** What a client of the node, such as the wallet, may read from it, and the
 *  requests it may make. The goal is that clients reach node state only through
 *  this class and need no node headers. The wallet still reads the chain directly
 *  under cs_main; that goes once the wallet keeps its own view of the chain. */
class Chain
{
public:
    virtual ~Chain() {}

    //! Fee rate to confirm within num_blocks, or CFeeRate(0) without enough data.
    virtual CFeeRate estimateSmartFee(int num_blocks, bool conservative, FeeCalculation* calc) const = 0;

    //! The highest confirmation target the fee estimator tracks.
    virtual unsigned int estimateMaxBlocks() const = 0;

    //! The lowest fee rate the mempool accepts.
    virtual CFeeRate mempoolMinFee() const = 0;

    //! Whether a mempool transaction has descendants in the mempool.
    virtual bool hasDescendantsInMempool(const uint256& txid) const = 0;

    //! Ancestor and descendant counts of a mempool transaction, both zero when it is absent.
    virtual void getTransactionAncestry(const uint256& txid, size_t& ancestors, size_t& descendants) const = 0;

    //! Whether tx would stay within the mempool's ancestor and descendant limits.
    virtual bool checkChainLimits(const CTransactionRef& tx) const = 0;

    //! BIP 125 replaceability of tx, from its own and its mempool ancestors' sequence numbers.
    virtual RBFTransactionState isRBFOptIn(const CTransaction& tx) const = 0;

    //! Adds tx to the mempool unless it is there already, then announces it to
    //! peers if relay is set. Returns false, with state set, if the mempool rejects it.
    virtual bool broadcastTransaction(const CTransactionRef& tx, CAmount max_tx_fee, bool relay, CValidationState& state) = 0;

    //! Where the wallets publish what the user should see.
    virtual WalletNotifications& notifications() const = 0;

    //! Check if the node is ready to broadcast transactions.
    virtual bool isReadyToBroadcast() = 0;
};

//! Return the Chain implementation over node, which must outlive it.
std::unique_ptr<Chain> MakeChain(NodeContext& node);

} // namespace interfaces

#endif // TAPYRUS_INTERFACES_CHAIN_H
