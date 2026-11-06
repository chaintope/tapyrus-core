// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_POLICY_POLICYSETTINGS_H
#define TAPYRUS_POLICY_POLICYSETTINGS_H

#include <amount.h>
#include <consensusconstants.h>
#include <policy/feerate.h>
#include <policy/policyconstants.h>

#include <cstdint>
#include <string>

class ArgsManager;
enum class TAPYRUS_OP_MODE;

/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const unsigned int DEFAULT_MIN_RELAY_TX_FEE = 1000;
/** Default for -incrementalrelayfee, which sets the minimum feerate increase for mempool limiting or BIP 125 replacement **/
static const unsigned int DEFAULT_INCREMENTAL_RELAY_FEE = 1000;
/** Min feerate for defining dust. Historically this has been based on the
 * minRelayTxFee, however changing the dust limit changes which transactions are
 * standard and should be done with care and ideally rarely. It makes sense to
 * only increase the dust limit after prior releases were already not creating
 * outputs below the new threshold */
static const unsigned int DUST_RELAY_TX_FEE = 3000;
/** Default for -blockmintxfee, which sets the minimum feerate for a transaction in blocks created by mining code **/
static const unsigned int DEFAULT_BLOCK_MIN_TX_FEE = 1000;
/** Default blockmax size, which controls the range of block size the mining code will create **/
static const unsigned int DEFAULT_BLOCK_MAX_SIZE = ConsensusConstants::MAX_BLOCK_SIZE - 1000;
/** Default for -bytespersigop */
static const unsigned int DEFAULT_BYTES_PER_SIGOP = 20;
static const bool DEFAULT_ACCEPT_DATACARRIER = true;
static const bool DEFAULT_ACCEPT_MULTIPLE_DATACARRIER = false;
/**
 * Default for -datacarriersize. 80 bytes of data, +1 for OP_RETURN,
 * +2 for the pushdata opcodes.
 */
static const unsigned int MAX_OP_RETURN_RELAY = 83;
/** Default for -mempoolreplacement */
static const bool DEFAULT_ENABLE_REPLACEMENT = true;
/** Default for -maxmempool, maximum megabytes of mempool memory usage */
static const unsigned int DEFAULT_MAX_MEMPOOL_SIZE = 300;
/** Default for -mempoolexpiry, expiration time for mempool transactions in hours */
static const unsigned int DEFAULT_MEMPOOL_EXPIRY = 336;
/** Default for -persistmempool */
static const bool DEFAULT_PERSIST_MEMPOOL = true;
/** Default for -limitancestorcount, max number of in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_LIMIT = 25;
/** Default for -limitancestorsize, maximum kilobytes of tx + all in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_SIZE_LIMIT = 101;
/** Default for -limitdescendantcount, max number of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_LIMIT = 25;
/** Default for -limitdescendantsize, maximum kilobytes of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_SIZE_LIMIT = 101;
/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;
/** Default for using fee filter */
static const bool DEFAULT_FEEFILTER = true;
/** Default for -whitelistrelay. */
static const bool DEFAULT_WHITELISTRELAY = true;
/** Default for -whitelistforcerelay. */
static const bool DEFAULT_WHITELISTFORCERELAY = true;
/** Default for -maxstandardtxsize, the largest transaction we relay and mine, capped at the block size limit; also caps orphan size */
static const unsigned int DEFAULT_MAX_STANDARD_TX_SIZE = 100000;
/** Default for -minstandardtxsize, the smallest transaction we relay and mine */
static const unsigned int DEFAULT_MIN_STANDARD_TX_SIZE = 82;
/**
 * Default for -maxscriptsigsize. The biggest standard txin is a 15-of-15 P2SH
 * multisig with compressed keys (remember the 520 byte limit on redeemScript
 * size). That works out to a (15*(33+1))+3=513 byte redeemScript,
 * 513+1+15*(73+1)+3=1627 bytes of scriptSig, which we round off to 1650 bytes
 * for some minor future-proofing. That's also enough to spend a 20-of-20
 * CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not standard.
 */
static const unsigned int DEFAULT_MAX_SCRIPTSIG_SIZE = 1650;
/** Default for -maxbaremultisigkeys: up to x-of-3 bare multisig is standard */
static const unsigned int DEFAULT_MAX_BAREMULTISIG_KEYS = 3;
/** Default for -maxp2shsigops, the most signature check operations in a standard P2SH script */
static const unsigned int DEFAULT_MAX_P2SH_SIGOPS = 15;
/** Default for -maxreplacementcandidates, the most mempool transactions one replacement may evict */
static const unsigned int DEFAULT_MAX_REPLACEMENT_CANDIDATES = 100;
/** Default for -orphanexpiry, expiration time for orphan transactions in seconds */
static const int64_t DEFAULT_ORPHAN_TX_EXPIRE_TIME = 20 * 60;
/** Seconds between sweeps of expired orphans, so also the shortest -orphanexpiry that can be honoured */
static constexpr int64_t ORPHAN_TX_EXPIRE_INTERVAL = 5 * 60;
/** Default for -maxpackagecount, the most transactions in a package */
static const unsigned int DEFAULT_MAX_PACKAGE_COUNT = 25;
// A package's transactions may form a single chain, which must fit the mempool's ancestor and
// descendant limits. Ensure that, at their defaults, those limits admit a full-size chain package.
static_assert(DEFAULT_DESCENDANT_LIMIT >= DEFAULT_MAX_PACKAGE_COUNT);
static_assert(DEFAULT_ANCESTOR_LIMIT >= DEFAULT_MAX_PACKAGE_COUNT);

/**
 * What this node relays, keeps in its mempool and mines: read once from
 * tapyrus.conf and the command line at start-up and never reloaded.
 */
struct PolicySettings : PolicyConstants
{
    /**
     * Reads and checks the policy options.
     * @throws std::runtime_error with the start-up error message if an option is invalid.
     */
    PolicySettings(const ArgsManager& args, TAPYRUS_OP_MODE mode);

    /** The minimum feerate increase for mempool limiting and BIP 125 replacement. */
    const CFeeRate incrementalRelayFee;
    /** Fees smaller than this are considered zero fee for relaying, mining and transaction creation. */
    const CFeeRate minRelayTxFee;
    /** Outputs worth less than they cost to spend at this feerate are dust. */
    const CFeeRate dustRelayFee;
    /** The lowest feerate of a transaction in blocks this node creates. */
    const CFeeRate blockMinTxFee;
    /** Equivalent bytes per sigop in transactions for relay and mining. */
    const uint32_t bytesPerSigOp;
    const bool acceptDatacarrier;
    /** Maximum size of TX_NULL_DATA scripts that this node considers standard. */
    const uint32_t maxDatacarrierBytes;
    const bool acceptMultipleDatacarrier;
    /** Relay and mine non-standard transactions (dev mode, DEBUG builds only). */
    const bool acceptNonStdTxn;
    const bool enableReplacement;

    /** Mempool memory limit, in bytes. */
    const int64_t maxMempool;
    /** Mempool transaction expiry, in seconds. */
    const int64_t mempoolExpiry;
    const bool persistMempool;
    const int64_t ancestorLimit;
    /** In bytes. */
    const int64_t ancestorSizeLimit;
    const int64_t descendantLimit;
    /** In bytes. */
    const int64_t descendantSizeLimit;

    const uint32_t maxOrphanTx;
    const int64_t blockReconstructionExtraTxn;
    const bool feeFilter;
    const bool whitelistRelay;
    const bool whitelistForceRelay;

    /** The -maxstandardtxsize option; use MaxStandardTxSize(), which caps it at the block size limit. */
    const uint32_t maxStandardTxSize;
    const uint32_t minStandardTxSize;
    const uint32_t maxScriptSigSize;
    const uint32_t maxBareMultisigKeys;
    const uint32_t maxP2shSigOps;
    const uint32_t maxReplacementCandidates;
    /** Orphan transaction expiry, in seconds. */
    const int64_t orphanExpiry;
    /** The most transactions in a package; CheckPackage bounds the chains inside it by the ancestor and descendant limits. */
    const uint32_t maxPackageCount;

    /** Size of the blocks this node creates, before clamping to the block size limit. */
    const uint32_t blockMaxSize;
    /** nFeatures of the blocks this node creates in dev mode. */
    const int32_t blockFeatures;

    /** The largest transaction we relay and mine: -maxstandardtxsize, capped at the latest block size limit. */
    uint32_t MaxStandardTxSize() const;

private:
    /** The fee rate of a -<name>=<amt> option, or its default when it is not set. */
    static CFeeRate ParseFeeRate(const ArgsManager& args, const std::string& name, CAmount defaultFee);
    static CFeeRate ParseMinRelayTxFee(const ArgsManager& args, const CFeeRate& incrementalRelayFee);
    static uint32_t ParseBytesPerSigOp(const ArgsManager& args);
    static uint32_t ParseMaxDatacarrierBytes(const ArgsManager& args);
    static bool ParseAcceptNonStdTxn(const ArgsManager& args, TAPYRUS_OP_MODE mode);
    static bool ParseEnableReplacement(const ArgsManager& args);
    static int64_t ParseMaxMempool(const ArgsManager& args);
    /** A -<name>=<n> option that must lie in [min, max]. */
    static int64_t ParseInRange(const ArgsManager& args, const std::string& name, int64_t defaultValue, int64_t min, int64_t max);
    static uint32_t ParseMinStandardTxSize(const ArgsManager& args, uint32_t maxStandardTxSize);
};

/**
 * Points at NodeContext's PolicySettings for code that has no context yet.
 * It goes once every user is passed the context.
 */
extern const PolicySettings* g_policy_settings;

#endif // TAPYRUS_POLICY_POLICYSETTINGS_H
