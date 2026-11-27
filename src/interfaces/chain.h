// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_INTERFACES_CHAIN_H
#define TAPYRUS_INTERFACES_CHAIN_H

#include <amount.h>
#include <primitives/transaction.h>

#include <cstdint>
#include <memory>
#include <stddef.h>

class CBlock;
class CBlockIndex;
class CFeeRate;
class CValidationState;
struct CBlockLocator;
struct NodeContext;
class uint256;
class WalletNotifications;
struct FeeCalculation;
enum class RBFTransactionState;

namespace interfaces {

/** What a client of the node, such as the wallet, may read from it, and the
 *  requests it may make. The goal is that clients reach node state only through
 *  this class and need no node headers. The wallet reads the chain only through
 *  this class, still as block index entries, which stay valid while the node runs. */
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

    //! The active chain's tip, or null before the genesis block is loaded.
    virtual CBlockIndex* getTip() = 0;
    //! The active chain's height, -1 when it is empty.
    virtual int getHeight() = 0;
    //! The active chain's genesis block, or null when it is empty.
    virtual CBlockIndex* getGenesis() = 0;
    //! The active chain's block at height, or null past the tip.
    virtual CBlockIndex* getBlockAtHeight(int height) = 0;
    //! The block after pindex in the active chain, or null at the tip or off it.
    virtual CBlockIndex* getNext(const CBlockIndex* pindex) = 0;
    //! Whether pindex is in the active chain.
    virtual bool isInActiveChain(const CBlockIndex* pindex) = 0;
    //! The last block pindex shares with the active chain.
    virtual const CBlockIndex* findFork(const CBlockIndex* pindex) = 0;
    //! The first active-chain block with a time of at least time, or null.
    virtual CBlockIndex* findEarliestAtLeast(int64_t time) = 0;
    //! The newest active-chain block the locator also has, else the genesis block.
    virtual CBlockIndex* findLocatorFork(const CBlockLocator& locator) = 0;
    //! The block index entry for hash, or null if the node does not know it.
    virtual CBlockIndex* lookupBlockIndex(const uint256& hash) = 0;
    //! A locator for the active chain's tip.
    virtual CBlockLocator getTipLocator() = 0;
    //! Reads pindex's block from disk; false if it is not stored.
    virtual bool readBlock(CBlock& block, const CBlockIndex* pindex) = 0;
    //! Estimated share of the chain's transactions up to pindex, from 0 to 1.
    virtual double guessVerificationProgress(const CBlockIndex* pindex) = 0;
    //! Whether the node prunes old block files.
    virtual bool isPruneMode() = 0;
    //! Whether the node is still in initial block download.
    virtual bool isInitialBlockDownload() = 0;
    //! The current maximum block size from the xfield history.
    virtual uint32_t getMaxBlockSize() = 0;
    //! Whether tx could be included in the next block by its lock time.
    virtual bool checkFinalTx(const CTransaction& tx) = 0;
};

//! Return the Chain implementation over node, which must outlive it.
std::unique_ptr<Chain> MakeChain(NodeContext& node);

} // namespace interfaces

#endif // TAPYRUS_INTERFACES_CHAIN_H
