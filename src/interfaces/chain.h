// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_INTERFACES_CHAIN_H
#define TAPYRUS_INTERFACES_CHAIN_H

#include <amount.h>
#include <primitives/transaction.h>

#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <stddef.h>
#include <stdint.h>

class CBlock;
struct CBlockLocator;
class CFeeRate;
class CKey;
class Coin;
class CPubKey;
class CReserveScript;
class CValidationState;
struct NodeContext;
class uint256;
class WalletNotifications;
struct FeeCalculation;
enum class RBFTransactionState;

namespace interfaces {

//! Helper for findBlock to selectively return pieces of block data. If block is
//! found, data will be returned by setting specified output variables. If block
//! is not found, output variables will keep their previous values.
class FoundBlock
{
public:
    FoundBlock& hash(uint256& hash) { m_hash = &hash; return *this; }
    FoundBlock& height(int& height) { m_height = &height; return *this; }
    FoundBlock& time(int64_t& time) { m_time = &time; return *this; }
    FoundBlock& maxTime(int64_t& max_time) { m_max_time = &max_time; return *this; }
    FoundBlock& mtpTime(int64_t& mtp_time) { m_mtp_time = &mtp_time; return *this; }
    //! Return whether block is in the active (most-work) chain.
    FoundBlock& inActiveChain(bool& in_active_chain) { m_in_active_chain = &in_active_chain; return *this; }
    //! Read block data from disk. If the block exists but doesn't have data
    //! (for example due to pruning), the CBlock variable will be set to null.
    FoundBlock& data(CBlock& data) { m_data = &data; return *this; }

    uint256* m_hash = nullptr;
    int* m_height = nullptr;
    int64_t* m_time = nullptr;
    int64_t* m_max_time = nullptr;
    int64_t* m_mtp_time = nullptr;
    bool* m_in_active_chain = nullptr;
    CBlock* m_data = nullptr;
};

/** What a client of the node, such as the wallet, may read from it, and the
 *  requests it may make. The goal is that clients reach node state only through
 *  this class and need no node headers. The wallet still reads the chain directly
 *  under cs_main; that goes once the wallet keeps its own view of the chain.
 *  Each method takes cs_main only around its own lookups and runs no client code
 *  while holding it, and reads block data from disk after releasing it, so the
 *  node never waits on a client. */
class Chain
{
public:
    virtual ~Chain() {}

    //! Get current chain height, not including genesis block (returns 0 if
    //! chain only contains genesis block, nullopt if chain does not contain
    //! any blocks)
    virtual std::optional<int> getHeight() = 0;

    //! Get block hash. Height must be valid or this function will abort.
    virtual uint256 getBlockHash(int height) = 0;

    //! Check that the block is available on disk (i.e. has not been
    //! pruned), and contains transactions.
    virtual bool haveBlockOnDisk(int height) = 0;

    //! Get locator for the current chain tip.
    virtual CBlockLocator getTipLocator() = 0;

    //! Return height of the highest block on chain in common with the locator,
    //! which will either be the original block used to create the locator,
    //! or one of its ancestors.
    virtual std::optional<int> findLocatorFork(const CBlockLocator& locator) = 0;

    //! Check if transaction will be final given chain height current time.
    virtual bool checkFinalTx(const CTransaction& tx) = 0;

    //! Return whether node has the block and optionally return block metadata
    //! or contents.
    virtual bool findBlock(const uint256& hash, const FoundBlock& block = {}) = 0;

    //! Find the block after block_hash at block_height on the active chain and
    //! optionally return its information. reorg is set when block_hash is no
    //! longer on the active chain at that height.
    virtual bool findNextBlock(const uint256& block_hash, int block_height, const FoundBlock& next = {}, bool* reorg = nullptr) = 0;

    //! Find first block in the chain with timestamp >= the given time
    //! and height >= than the given height, return false if there is no block
    //! with a high enough timestamp and height. Optionally return block
    //! information.
    virtual bool findFirstBlockWithTimeAndHeight(int64_t min_time, int min_height, const FoundBlock& block = {}) = 0;

    //! Find ancestor of block at specified height and optionally return
    //! ancestor information.
    virtual bool findAncestorByHeight(const uint256& block_hash, int ancestor_height, const FoundBlock& ancestor_out = {}) = 0;

    //! Return whether block descends from a specified ancestor, and
    //! optionally return ancestor information.
    virtual bool findAncestorByHash(const uint256& block_hash,
        const uint256& ancestor_hash,
        const FoundBlock& ancestor_out = {}) = 0;

    //! Find most recent common ancestor between two blocks and optionally
    //! return block information.
    virtual bool findCommonAncestor(const uint256& block_hash1,
        const uint256& block_hash2,
        const FoundBlock& ancestor_out = {},
        const FoundBlock& block1_out = {},
        const FoundBlock& block2_out = {}) = 0;

    //! Return true if data is available for all blocks in the specified range
    //! of blocks. This checks all blocks that are ancestors of block_hash in
    //! the height range from min_height to max_height, inclusive.
    virtual bool hasBlocks(const uint256& block_hash, int min_height = 0, std::optional<int> max_height = {}) = 0;

    //! Estimate fraction of total transactions verified if blocks up to
    //! the specified block hash are verified.
    virtual double guessVerificationProgress(const uint256& block_hash) = 0;

    //! Check if any block has been pruned.
    virtual bool havePruned() = 0;

    //! Whether the node runs with -prune, whether or not it has pruned yet.
    virtual bool isPruneMode() = 0;

    //! Check if in IBD.
    virtual bool isInitialBlockDownload() = 0;

    //! Wait for pending notifications to be processed unless block hash points to the current
    //! chain tip.
    virtual void waitForNotificationsIfTipChanged(const uint256& old_tip) = 0;

    //! The block size limit the latest xfield sets.
    virtual uint32_t getMaxBlockSize() = 0;

    //! The aggregate public key the latest xfield sets; blocks are signed with it.
    virtual CPubKey getAggregatePubkey() = 0;

    //! Fills each coin from the mempool, else from the UTXO set; one found in
    //! neither is cleared, so it reads as spent.
    virtual void findCoins(std::map<COutPoint, Coin>& coins) = 0;

    //! A transaction from the mempool, the transaction index, or the block of
    //! an unspent output of it; null if none has it.
    virtual CTransactionRef findTransaction(const uint256& txid) = 0;

    //! Mines and signs num_blocks blocks on the tip with privkey, paying
    //! coinbase_script, which is kept once used. Returns the block hashes, and
    //! throws, as the RPCs do, if a block cannot be made or is not accepted.
    virtual std::vector<uint256> generateBlocks(std::shared_ptr<CReserveScript> coinbase_script, int num_blocks, const CKey& privkey) = 0;

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
