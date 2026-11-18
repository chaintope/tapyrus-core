// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockfilemanager.h>
#include <consensussettings.h>
#include <contextualconsensusrules.h>
#include <checkblock.h>
#include <consensus/tx_check.h>
#include <mempoolaccept.h>
#include <validation.h>
#include <validationcache.h>
#include <cs_main.h>
#include <issuedcolorids.h>
#include <chainstatemanager.h>
#include <checkpoints.h>
#include <checkqueue.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <federationparams.h>
#include <hash.h>
#include <index/txindex.h>
#include <policy/packages.h>
#include <policy/policy.h>
#include <policy/policyconstants.h>
#include <policy/rbf.h>
#include <precomputedcolordata.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/sigcache.h>
#include <shutdown.h>
#include <tinyformat.h>
#include <transactioncolorchecker.h>
//#include <timedata.h>
#include <trace.h>
#include <ui_interface.h>
#include <utilmoneystr.h>
#include <tempxfieldhistory.h>

#include <future>
#include <thread>
#include <sstream>

#if defined(NDEBUG)
# error "Tapyrus cannot be compiled without assertions."
#endif

class ConnectTrace;

extern RecursiveMutex cs_main;

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    BlockIndexMap& blockIndex = g_chainstate_manager.BlockIndex();
    AssertLockHeld(cs_main);

    // Find the latest block common to locator and chain - we expect that
    // locator.vHave is sorted descending by height.
    for (const uint256& hash : locator.vHave) {
        CBlockIndex* pindex = blockIndex.LookupBlockIndex(hash);
        if (pindex) {
            if (chain.Contains(pindex))
                return pindex;
            if (pindex->GetAncestor(chain.Height()) == chain.Tip()) {
                return chain.Tip();
            }
        }
    }
    return chain.Genesis();
}


const CBlockIndex* GetLastCheckpointIfEnabled()
{
    AssertLockHeld(cs_main);
    return g_chainstate_manager.GetOptions().fCheckpointsEnabled ? Checkpoints::GetLastCheckpoint(g_chainstate_manager.Settings().Checkpoints()) : nullptr;
}

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses the active chain height + 1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than the active chain height.
    const int nBlockHeight = g_chainstate_manager.ActiveChain().Height() + 1;

    // BIP113 requires that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & ConsensusConstants::LOCKTIME_MEDIAN_TIME_PAST)
                             ? g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast()
                             : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

bool TestLockPointValidity(const LockPoints* lp)
{
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the chain
    if (lp->maxInputBlock) {
        // Check whether the active chain is an extension of the block at which the LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!g_chainstate_manager.ActiveChain().Contains(lp->maxInputBlock)) {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

bool CheckSequenceLocks(const CTxMemPool& pool, const CTransaction &tx, int flags, CCoinsViewMemPool& viewMemPool, LockPoints* lp, bool useExistingLockPoints)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(pool.cs);

    CBlockIndex* tip = g_chainstate_manager.ActiveChain().Tip();
    assert(tip != nullptr);

    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses the active chain height + 1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than the active chain height
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else {
        // The coins tip contains the UTXO set for the active tip
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn& txin = tx.vin[txinIndex];
            Coin coin;
            if (!viewMemPool.GetCoin(txin.prevout, coin)) {
                return error("%s: Missing input", __func__);
            }
            if (coin.nHeight == MEMPOOL_HEIGHT) {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else {
                prevheights[txinIndex] = coin.nHeight;
            }
        }
        lockPair = ContextualConsensusRules::CalculateSequenceLocks(tx, flags, &prevheights, index);
        if (lp) {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            for (int height : prevheights) {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight+1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return ContextualConsensusRules::EvaluateSequenceLocks(index, lockPair);
}
void LimitMempoolSize(CTxMemPool& pool) {
    int expired = pool.Expire(GetTime() - pool.Expiry());
    if (expired != 0) {
        LogPrint(BCLog::MEMPOOL, "Expired %i transactions from the memory pool\n", expired);
    }

    std::vector<COutPoint> vNoSpendsRemaining;
    pool.TrimToSize(pool.MaxSize(), &vNoSpendsRemaining);
    for (const COutPoint& removed : vNoSpendsRemaining)
        g_chainstate_manager.ActiveChainstate().CoinsTip().Uncache(removed);
}

/** Convert CValidationState to a human-readable message for logging */
// FormatStateMessage() moved to checkblock.cpp -- see the file-level
// comment there for why.

static bool IsCurrentForFeeEstimation()
{
    AssertLockHeld(cs_main);
    if (IsInitialBlockDownload())
        return false;
    if (g_chainstate_manager.ActiveChain().Tip()->GetBlockTime() < (GetTime() - MAX_FEE_ESTIMATION_TIP_AGE))
        return false;
    if (g_chainstate_manager.ActiveChain().Height() < g_chainstate_manager.BestHeader()->nHeight - 1)
        return false;
    return true;
}

// Used to avoid mempool polluting consensus critical paths if CCoinsViewMempool
// were somehow broken and returning the wrong scriptPubKeys
static bool CheckInputsFromMempoolAndCache(bool in_package, const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view, const CTxMemPool& pool,
                 unsigned int flags, bool cacheSigStore) {
    AssertLockHeld(cs_main);

    // pool.cs should be locked already, but go ahead and re-take the lock here
    // to enforce that mempool doesn't change between when we check the view
    // and when we actually call through to CheckInputs
    LOCK(pool.cs);

    assert(!tx.IsCoinBase());
    for (const CTxIn& txin : tx.vin) {
        const Coin& coin = view.AccessCoin(txin.prevout);

        // At this point we haven't actually checked if the coins are all
        // available (or shouldn't assume we have, since CheckInputs does).
        // So we just return failure if the inputs are not available here,
        // and then only have to check equivalence for available inputs.
        if (coin.IsSpent()) return false;

        const CTransactionRef& txFrom = pool.get(txin.prevout.hashMalFix);
        if (txFrom) {
            assert(txFrom->GetHashMalFix() == txin.prevout.hashMalFix);
            assert(txFrom->vout.size() > txin.prevout.n);
            assert(txFrom->vout[txin.prevout.n] == coin.out);
        } else if (!in_package) { //transactions in a package are not expected to be present in the disk
                const Coin& coinFromDisk = g_chainstate_manager.ActiveChainstate().CoinsTip().AccessCoin(txin.prevout);
                assert(!coinFromDisk.IsSpent());
                assert(coinFromDisk.out == coin.out);
        }
    }

    return CheckInputs(tx, state, view, true, flags, cacheSigStore, true, g_chainstate_manager.ValidationCaches());
}

static bool CheckConflictsInMempool(const CTxMemPool& pool, const CTransaction& tx, std::set<uint256>& setConflicts, CValidationState& state)
{
    for (const CTxIn &txin : tx.vin)
    {
        AssertLockHeld(pool.cs);
        auto itConflicting = pool.mapNextTx.find(txin.prevout);
        if (itConflicting != pool.mapNextTx.end())
        {
            const CTransaction *ptxConflicting = itConflicting->second;
            if (!setConflicts.count(ptxConflicting->GetHashMalFix()))
            {
                // Allow opt-out of transaction replacement by setting
                // nSequence > MAX_BIP125_RBF_SEQUENCE (SEQUENCE_FINAL-2) on all inputs.
                //
                // SEQUENCE_FINAL-1 is picked to still allow use of nLockTime by
                // non-replaceable transactions. All inputs rather than just one
                // is for the sake of multi-party protocols, where we don't
                // want a single party to be able to disable replacement.
                //
                // The opt-out ignores descendants as anyone relying on
                // first-seen mempool behavior should be checking all
                // unconfirmed ancestors anyway; doing otherwise is hopelessly
                // insecure.
                bool fReplacementOptOut = true;
                if (g_policy_settings->enableReplacement)
                {
                    for (const CTxIn &_txin : ptxConflicting->vin)
                    {
                        if (_txin.nSequence <= MAX_BIP125_RBF_SEQUENCE)
                        {
                            fReplacementOptOut = false;
                            break;
                        }
                    }
                }
                if (fReplacementOptOut) {
                    return state.Invalid(false, REJECT_DUPLICATE, "txn-mempool-conflict");
                }
                setConflicts.insert(ptxConflicting->GetHashMalFix());
            }
        }
    }
    return true;
}

static bool DoAllInputsExist(const CTransaction& tx, CValidationState& state, std::vector<COutPoint>& coins_to_uncache, std::vector<COutPoint>& missing_inputs, CCoinsViewCache &view) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    for (const CTxIn& txin : tx.vin) {
        if (!g_chainstate_manager.ActiveChainstate().CoinsTip().HaveCoinInCache(txin.prevout)) {
            coins_to_uncache.push_back(txin.prevout);
        }
        if (!view.HaveCoin(txin.prevout)) {
            // Are inputs missing because we already have the tx?
            for (size_t out = 0; out < tx.vout.size(); out++) {
                // Optimistically just do efficient check of cache for outputs
                if (g_chainstate_manager.ActiveChainstate().CoinsTip().HaveCoinInCache(COutPoint(tx.GetHashMalFix(), out))) {
                    return state.Invalid(false, REJECT_DUPLICATE, "txn-already-known");
                }
            }
            // Otherwise assume this might be an orphan tx for which we just haven't seen parents yet
            missing_inputs.push_back(txin.prevout);

            return false; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
        }
    }
    return true;
}

static bool AcceptToMemoryPoolWorker(CTxMemPool& pool, const CTransactionRef &ptx, const MempoolAcceptOptions& options, CCoinsViewMemPool& mempool_view, bool in_package,
                                     std::vector<COutPoint>& coins_to_uncache, MempoolAcceptResult& result)
{
    const CTransaction& tx = *ptx;
    const uint256 hash = tx.GetHashMalFix();
    AssertLockHeld(cs_main);

    CValidationState& state = result.state;
    LOCK(pool.cs); // mempool "read lock" (held through GetMainSignals().TransactionAddedToMempool())

    if (!CheckTransaction(tx, state, g_chainstate_manager.XFieldHistory().GetCurrentMaxBlockSize()))
        return false; // state filled in by CheckTransaction

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");

    // Rather not work on nonstandard transactions (unless -dev)
    std::string reason;
    if (!g_policy_settings->acceptNonStdTxn && !IsStandardTx(tx, reason))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);
    // Do not work on transactions that are too small.
    // Transactions smaller than this are not relayed to reduce unnecessary malloc overhead.
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) < g_policy_settings->MinStandardTxSize())
        return state.DoS(0, false, REJECT_NONSTANDARD, "tx-size-small");

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, PolicyConstants::STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

    // is it already in the memory pool?
    if (pool.exists(hash)) {
        return state.Invalid(false, REJECT_DUPLICATE, "txn-already-in-mempool");
    }

    // Check for conflicts with in-memory transactions
    std::set<uint256> setConflicts;
    if(!CheckConflictsInMempool(pool, tx, setConflicts, state))
        return false;

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        view.SetBackend(mempool_view);

        // do all inputs exist?
        if(!DoAllInputsExist(tx, state, coins_to_uncache, result.missing_inputs, view))
            return false;

        // Read once here; the balance check below reuses it after the view
        // is switched away from the mempool.
        PrecomputedColorData colorData(tx, view, g_chainstate_manager.ActiveChain().Tip()->nHeight + 1);
        colorData.SetIssued(g_chainstate_manager.IssuedColorIds());
        const TransactionColorChecker colorChecker(colorData);
        if (!colorChecker.CheckOutputColorIds(state))
            return false;

        // Bring the best block into scope
        view.GetBestBlock();

        // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
        view.SetBackend(dummy);

        // Only accept BIP68 sequence locked transactions that can be mined in the next
        // block; we don't want our mempool filled up with transactions that can't
        // be mined yet.
        // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
        // CoinsViewCache instead of create its own
        LockPoints lp;
        if (!CheckSequenceLocks(pool, tx, PolicyConstants::STANDARD_LOCKTIME_VERIFY_FLAGS, mempool_view, &lp))
            return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");

        CAmount nFees = 0;
        if (!ContextualConsensusRules::CheckTxInputs(tx, state, view, GetSpendHeight(view), nFees)) {
            return error("%s: Consensus::CheckTxInputs: %s, %s", __func__, tx.GetHashMalFix().ToString(), FormatStateMessage(state));
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (!AreInputsStandard(tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        uint32_t nSigOps = ContextualConsensusRules::GetTransactionSigOps(tx, view, PolicyConstants::STANDARD_SCRIPT_VERIFY_FLAGS);

        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        pool.ApplyDelta(hash, nModifiedFees);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        for (const CTxIn &txin : tx.vin) {
            const Coin &coin = view.AccessCoin(txin.prevout);
            if (coin.IsCoinBase())
            {
                fSpendsCoinbase = true;
                break;
            }
        }

        CTxMemPoolEntry entry(ptx, nFees, options.accept_time, g_chainstate_manager.ActiveChain().Height(),
                              fSpendsCoinbase, nSigOps, lp);
        unsigned int nSize = entry.GetTxSize();

        CAmount mempoolRejectFee = pool.GetMinFee().GetFee(nSize);
        if (!options.bypass_limits
          && mempoolRejectFee > 0
          && nModifiedFees < mempoolRejectFee) {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool min fee not met", false, strprintf("%d < %d", nModifiedFees, mempoolRejectFee));
        }

        // No transactions are allowed below minRelayTxFee except from disconnected blocks
        if (!options.bypass_limits
          && nModifiedFees < g_policy_settings->minRelayTxFee.GetFee(nSize)) {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "min relay fee not met", false, strprintf("%d < %d", nModifiedFees, g_policy_settings->minRelayTxFee.GetFee(nSize)));
        }

        if (options.max_tx_fee && nFees > options.max_tx_fee)
            return state.Invalid(false,
                REJECT_HIGHFEE, "absurdly-high-fee",
                strprintf("%d > %d", nFees, options.max_tx_fee));

        // Calculate in-mempool ancestors, up to a limit.
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = g_policy_settings->ancestorLimit;
        size_t nLimitAncestorSize = g_policy_settings->ancestorSizeLimit;
        size_t nLimitDescendants = g_policy_settings->descendantLimit;
        size_t nLimitDescendantSize = g_policy_settings->descendantSizeLimit;
        std::string errString;
        if (!pool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
        }

        // A transaction that spends outputs that would be replaced by it is invalid. Now
        // that we have the set of all ancestors we can detect this
        // pathological case by making sure setConflicts and setAncestors don't
        // intersect.
        for (CTxMemPool::txiter ancestorIt : setAncestors)
        {
            const uint256 &hashAncestor = ancestorIt->GetTx().GetHashMalFix();
            if (setConflicts.count(hashAncestor))
            {
                return state.DoS(10, false,
                                 REJECT_INVALID, "bad-txns-spends-conflicting-tx", false,
                                 strprintf("%s spends conflicting transaction %s",
                                           hash.ToString(),
                                           hashAncestor.ToString()));
            }
        }

        // Check if it's economically rational to mine this transaction rather
        // than the ones it replaces.
        CAmount nConflictingFees = 0;
        size_t nConflictingSize = 0;
        uint64_t nConflictingCount = 0;
        CTxMemPool::setEntries allConflicting;

        // If we don't hold the lock allConflicting might be incomplete; the
        // subsequent RemoveStaged() and addUnchecked() calls don't guarantee
        // mempool consistency for us.
        const bool fReplacementTransaction = setConflicts.size();
        if (fReplacementTransaction)
        {
            CFeeRate newFeeRate(nModifiedFees, nSize);
            std::set<uint256> setConflictsParents;
            const uint32_t maxDescendantsToVisit = g_policy_settings->maxReplacementCandidates;
            CTxMemPool::setEntries setIterConflicting;
            for (const uint256 &hashConflicting : setConflicts)
            {
                CTxMemPool::txiter mi = pool.mapTx.find(hashConflicting);
                if (mi == pool.mapTx.end())
                    continue;

                // Save these to avoid repeated lookups
                setIterConflicting.insert(mi);

                // Don't allow the replacement to reduce the feerate of the
                // mempool.
                //
                // We usually don't want to accept replacements with lower
                // feerates than what they replaced as that would lower the
                // feerate of the next block. Requiring that the feerate always
                // be increased is also an easy-to-reason about way to prevent
                // DoS attacks via replacements.
                //
                // We only consider the feerates of transactions being directly
                // replaced, not their indirect descendants. While that does
                // mean high feerate children are ignored when deciding whether
                // or not to replace, we do require the replacement to pay more
                // overall fees too, mitigating most cases.
                CFeeRate oldFeeRate(mi->GetModifiedFee(), mi->GetTxSize());
                if (newFeeRate <= oldFeeRate)
                {
                    return state.DoS(0, false,
                            REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                            strprintf("rejecting replacement %s; new feerate %s <= old feerate %s",
                                  hash.ToString(),
                                  newFeeRate.ToString(),
                                  oldFeeRate.ToString()));
                }

                for (const CTxIn &txin : mi->GetTx().vin)
                {
                    setConflictsParents.insert(txin.prevout.hashMalFix);
                }

                nConflictingCount += mi->GetCountWithDescendants();
            }
            // This potentially overestimates the number of actual descendants
            // but we just want to be conservative to avoid doing too much
            // work.
            if (nConflictingCount <= maxDescendantsToVisit) {
                // If not too many to replace, then calculate the set of
                // transactions that would have to be evicted
                for (CTxMemPool::txiter it : setIterConflicting) {
                    pool.CalculateDescendants(it, allConflicting);
                }
                for (CTxMemPool::txiter it : allConflicting) {
                    nConflictingFees += it->GetModifiedFee();
                    nConflictingSize += it->GetTxSize();
                }
            } else {
                return state.DoS(0, false,
                        REJECT_NONSTANDARD, "too many potential replacements", false,
                        strprintf("rejecting replacement %s; too many potential replacements (%d > %d)\n",
                            hash.ToString(),
                            nConflictingCount,
                            maxDescendantsToVisit));
            }

            for (unsigned int j = 0; j < tx.vin.size(); j++)
            {
                // We don't want to accept replacements that require low
                // feerate junk to be mined first. Ideally we'd keep track of
                // the ancestor feerates and make the decision based on that,
                // but for now requiring all new inputs to be confirmed works.
                if (!setConflictsParents.count(tx.vin[j].prevout.hashMalFix))
                {
                    // Rather than check the UTXO set - potentially expensive -
                    // it's cheaper to just check if the new input refers to a
                    // tx that's in the mempool.
                    if (pool.mapTx.find(tx.vin[j].prevout.hashMalFix) != pool.mapTx.end())
                        return state.DoS(0, false,
                                         REJECT_NONSTANDARD, "replacement-adds-unconfirmed", false,
                                         strprintf("replacement %s adds unconfirmed input, idx %d",
                                                  hash.ToString(), j));
                }
            }

            // The replacement must pay greater fees than the transactions it
            // replaces - if we did the bandwidth used by those conflicting
            // transactions would not be paid for.
            if (nModifiedFees < nConflictingFees)
            {
                return state.DoS(0, false,
                                 REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                                 strprintf("rejecting replacement %s, less fees than conflicting txs; %s < %s",
                                          hash.ToString(), FormatMoney(nModifiedFees), FormatMoney(nConflictingFees)));
            }

            // Finally in addition to paying more fees than the conflicts the
            // new transaction must pay for its own bandwidth.
            CAmount nDeltaFees = nModifiedFees - nConflictingFees;
            if (nDeltaFees < g_policy_settings->incrementalRelayFee.GetFee(nSize))
            {
                return state.DoS(0, false,
                        REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                        strprintf("rejecting replacement %s, not enough additional fees to relay; %s < %s",
                              hash.ToString(),
                              FormatMoney(nDeltaFees),
                              FormatMoney(g_policy_settings->incrementalRelayFee.GetFee(nSize))));
            }
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        // Query flags for the *next* block (tip+1), not the tip, so that softfork
        // activation at height H is enforced by the mempool at tip == H-1.
        // Using tip height here would admit txs that are valid pre-activation but
        // invalid post-activation, producing an invalid block at height H.
        const unsigned int tipScriptFlags = g_chainstate_manager.Settings().ScriptFlagsAt(g_chainstate_manager.ActiveChain().Tip()->nHeight + 1);
        const unsigned int mempoolScriptFlags = PolicyConstants::STANDARD_SCRIPT_VERIFY_FLAGS | tipScriptFlags;
        // Pass tipScriptFlags as mandatoryFlags so that softfork flags active at
        // nextBlockHeight are treated as mandatory even when they also appear in
        // PolicyConstants::STANDARD_SCRIPT_VERIFY_FLAGS (e.g. SCRIPT_VERIFY_CP2SH_COLORED).
        if (!CheckInputs(tx, state, view, true, mempoolScriptFlags, true, false, g_chainstate_manager.ValidationCaches(), nullptr, tipScriptFlags)) {
            return false; // state filled in by CheckInputs
        }

        // Cache script execution results using the same next-block flags so the
        // cache entries are valid when ConnectBlock runs at height nextBlockHeight.
        if (!CheckInputsFromMempoolAndCache(in_package, tx, state, view, pool, tipScriptFlags, true)) {
            return error("%s: BUG! PLEASE REPORT THIS! CheckInputs failed against latest-block but not STANDARD flags %s, %s",
                    __func__, hash.ToString(), FormatStateMessage(state));
        }

        if (!colorChecker.CheckTokenBalances(state, g_policy_settings->minRelayTxFee.GetFee(nSize)))
            return false;

        if (options.test_only) {
            // Tx was accepted, but not added
            return true;
        }

        // Remove conflicting transactions from the mempool
        for (CTxMemPool::txiter it : allConflicting)
        {
            LogPrint(BCLog::MEMPOOL, "replacing tx %s with %s for %s TPC additional fees, %d delta bytes\n",
                    it->GetTx().GetHashMalFix().ToString(),
                    hash.ToString(),
                    FormatMoney(nModifiedFees - nConflictingFees),
                    (int)nSize - (int)nConflictingSize);
            TRACE7(mempool, replaced,
                it->GetTx().GetHashMalFix().begin(),
                it->GetTxSize(),
                nModifiedFees,
                it->GetTime(),
                hash.begin(),
                tx.GetTotalSize(),
                nConflictingFees
            );
            result.replaced.push_back(it->GetSharedTx());
        }
        pool.RemoveStaged(allConflicting, false, MemPoolRemovalReason::REPLACED);

        // This transaction should only count for fee estimation if:
        // - it isn't a BIP 125 replacement transaction (may not be widely supported)
        // - it's not being re-added during a reorg which bypasses typical mempool fee limits
        // - the node is not behind
        // - the transaction is not dependent on any other transactions in the mempool
        bool validForFeeEstimation = !fReplacementTransaction && !options.bypass_limits && IsCurrentForFeeEstimation() && pool.HasNoInputsOf(tx);

        // Store transaction in memory
        pool.addUnchecked(hash, entry, setAncestors, validForFeeEstimation);

        // trim mempool and check if tx was trimmed
        if (!options.bypass_limits) {
            LimitMempoolSize(pool);
            if (!pool.exists(hash))
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool full");
        }
    }

    GetMainSignals().TransactionAddedToMempool(ptx);

    return true;
}

bool Chainstate::AcceptToMemoryPool(const CTransactionRef& tx, const MempoolAcceptOptions& options, MempoolAcceptResult& result)
{
    AssertLockHeld(cs_main);
    CCoinsViewMemPool view(&CoinsTip(), m_mempool);
    return AcceptSingleTransaction(tx, options, view, /*in_package=*/false, result);
}

bool Chainstate::AcceptSingleTransaction(const CTransactionRef& tx, const MempoolAcceptOptions& options, CCoinsViewMemPool& view, bool in_package, MempoolAcceptResult& result)
{
    AssertLockHeld(cs_main);
    // TODO: options.accept_time is not used; the entry records the current time.
    MempoolAcceptOptions stamped{options};
    stamped.accept_time = GetTime();
    std::vector<COutPoint> coins_to_uncache;
    bool res = AcceptToMemoryPoolWorker(m_mempool, tx, stamped, view, in_package, coins_to_uncache, result);
    result.state.missingInputs = !result.missing_inputs.empty();
    if (!res) {
        for (const COutPoint& hashTx : coins_to_uncache)
        {
            CoinsTip().Uncache(hashTx);
            TRACE2(mempool, rejected,
                tx->GetHashMalFix().begin(),
                result.state.GetRejectReason().c_str());
        }
    }
    // After we've (potentially) uncached entries, ensure our coins cache is still within its size limits
    CValidationState stateDummy;
    g_chainstate_manager.ActiveChainstate().FlushStateToDisk(stateDummy, FlushStateMode::PERIODIC);
    return res;
}

void Chainstate::FilterMempoolDuplicates(const std::vector<CTransaction>& txns, Package& package, PackageValidationState& results) const
{
    for (const auto& tx : txns) {
        if (m_mempool.exists(tx.GetHashMalFix())) {
            CValidationState state;
            state.Invalid(false, REJECT_DUPLICATE, "txn-already-in-mempool");
            results.emplace(tx.GetHashMalFix(), state);
        } else {
            package.emplace_back(MakeTransactionRef(tx));
        }
    }
}

bool Chainstate::SubmitPackageToMempool(const Package& package,
                                        CValidationState& state,
                                        PackageValidationState& results,
                                        const MempoolAcceptOptions& options)
{
    if(!CheckPackage(package, state))
        return false;

    // A test-only dry run reads inputs through a virtual mempool, so each tx in a
    // chained package can spend outputs of earlier package txs that passed but
    // were not admitted.
    std::unique_ptr<CCoinsViewMemPool> view;
    CCoinsViewVirtualMemPool* virtualView = nullptr;

    bool all_admitted = false;
    Package relay;
    {
        // Held for the whole package so no block connects mid-package and the
        // results below describe only what the package itself did to the mempool.
        LOCK(::cs_main);
        if (options.test_only) {
            auto virtual_view = std::make_unique<CCoinsViewVirtualMemPool>(&CoinsTip(), m_mempool);
            virtualView = virtual_view.get();
            view = std::move(virtual_view);
        } else {
            view = std::make_unique<CCoinsViewMemPool>(&CoinsTip(), m_mempool);
        }
        std::set<uint256> replaced;
        for(auto &tx : package)
        {
            MempoolAcceptResult result;
            AcceptSingleTransaction(tx, options, *view, /*in_package=*/true, result);
            for (const auto& replaced_tx : result.replaced) {
                replaced.insert(replaced_tx->GetHashMalFix());
            }
            results.emplace(tx->GetHashMalFix(), result.state);

            // Only add to the virtual overlay if the tx was TRULY accepted.
            // IsValid() returns true even when missingInputs is set (DoAllInputsExist
            // deliberately avoids calling state.Invalid() so callers can distinguish
            // orphans from consensus failures).  Adding a missing-inputs tx to the
            // overlay would let its outputs be found by downstream txs, causing them
            // to spuriously pass test-only validation.
            if (virtualView && result.state.IsValid() && !result.state.missingInputs) {
                virtualView->AddVirtualTx(*tx);
            }
        }

        // Taken before the results below are rewritten, so a tx a later package tx
        // pushed out does not stop the rest of the package from being relayed.
        // Only the package's own txs count: results also holds the txs
        // FilterMempoolDuplicates found already in the mempool.
        all_admitted = true;
        for (const auto& tx : package) {
            const auto result = results.find(tx->GetHashMalFix());
            if (result == results.end() || result->second.IsInvalid() || result->second.IsError() || result->second.missingInputs) {
                all_admitted = false;
                break;
            }
        }

        // A later package tx can trim or replace one accepted before it, so report
        // and relay what the mempool holds now instead of what each tx saw on entry.
        if (!options.test_only) {
            for (const auto& tx : package) {
                const uint256 hash = tx->GetHashMalFix();
                if (m_mempool.exists(hash)) {
                    relay.push_back(tx);
                    continue;
                }
                const auto result = results.find(hash);
                if (result == results.end() || !result->second.IsValid() || result->second.missingInputs) {
                    continue;
                }
                CValidationState removed;
                if (replaced.count(hash)) {
                    removed.Invalid(false, REJECT_PACKAGE_TX, "replaced-in-package");
                } else {
                    removed.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool full");
                }
                results.erase(result);
                results.emplace(hash, removed);
            }
        }
    }

    bool success = ArePackageTransactionsAccepted(results);

    // Relay only after every tx in the package has been admitted, and only the
    // txs still in the mempool. Relaying per-tx inside the loop would gossip the
    // admitted prefix of a partially-failed package, enabling free relay
    // amplification attacks. A test-only dry run never relays.
    // Goes through PackageTransactionsRelay() rather than calling into net
    // directly -- tapyrus_chainstate (this file) has no business linking
    // tapyrus_peer. See the comment on CValidationInterface::
    // PackageTransactionsRelay() and PeerManager's override of it.
    if (!options.test_only && all_admitted && !relay.empty()) {
        GetMainSignals().PackageTransactionsRelay(relay);
    }

    return success;
}

/**
 * Return transaction in txOut, and if it was found inside a block, its hash is placed in hashBlock.
 * If blockIndex is provided, the transaction is fetched from the corresponding block.
 */
bool GetTransaction(const CTxMemPool* mempool, const uint256& hash, CTransactionRef& txOut, uint256& hashBlock, bool fAllowSlow, CBlockIndex* blockIndex)
{
    CBlockIndex* pindexSlow = blockIndex;

    LOCK(cs_main);

    if (!blockIndex) {
        if (mempool) {
            CTransactionRef ptx = mempool->get(hash);
            if (ptx) {
                txOut = ptx;
                return true;
            }
        }

        if (g_txindex) {
            return g_txindex->FindTx(hash, hashBlock, txOut);
        }

        if (fAllowSlow) { // use coin database to locate block that contains transaction, and scan it
            const Coin& coin = AccessByTxid(g_chainstate_manager.ActiveChainstate().CoinsTip(), hash);
            if (!coin.IsSpent()) pindexSlow = g_chainstate_manager.ActiveChain()[coin.nHeight];
        }
    }

    if (pindexSlow) {
        CBlock block;
        if (g_chainstate_manager.ReadBlock(block, pindexSlow)) {
            for (const auto& tx : block.vtx) {
                if (tx->GetHashMalFix() == hash) {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    return true;
                }
            }
        }
    }

    return false;
}

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool IsInitialBlockDownload()
{
    // Once this function has returned false, it must remain false.
    static std::atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;

    LOCK(cs_main);
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;
    if (g_chainstate_manager.LoadingBlocks())
        return true;
    if (g_chainstate_manager.ActiveChain().Tip() == nullptr)
        return true;
    if (g_chainstate_manager.ActiveChain().Tip()->GetBlockTime() < (GetTime() - g_chainstate_manager.GetOptions().nMaxTipAge))
        return true;
    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    latchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

std::optional<ScriptError> CScriptCheck::operator()() {
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    ScriptError error{SCRIPT_ERR_UNKNOWN_ERROR};
    if (VerifyScript(scriptSig, m_tx_out.scriptPubKey, nFlags,
                     CachingTransactionSignatureChecker(ptxTo, nIn, m_tx_out.nValue, cacheStore, *m_signature_cache),
                     &error))
        return std::nullopt;
    return error;
}

int GetSpendHeight(const CCoinsViewCache& inputs)
{
    BlockIndexMap& blockIndex = g_chainstate_manager.BlockIndex();
    LOCK(cs_main);
    CBlockIndex* pindexPrev = blockIndex.LookupBlockIndex(inputs.GetBestBlock());
    return pindexPrev->nHeight + 1;
}

bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore, ValidationCache& validation_cache, std::vector<CScriptCheck> *pvChecks, unsigned int mandatoryFlags)
{
    if (!tx.IsCoinBase())
    {
        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip script verification when connecting blocks under the
        // assumevalid block. Assuming the assumevalid block is valid this
        // is safe because block merkle hashes are still computed and checked,
        // Of course, if an assumed valid block is invalid due to false scriptSigs
        // this optimization would allow an invalid chain to be accepted.
        if (fScriptChecks) {
            // First check if script executions have been cached with the same
            // flags. Note that this assumes that the inputs provided are
            // correct (ie that the transaction hash which is in tx's prevouts
            // properly commits to the scriptPubKey in the inputs view of that
            // transaction).
            const uint256 hashCacheEntry = validation_cache.ScriptExecutionEntry(tx.GetHash(), flags);
            AssertLockHeld(cs_main); //TODO: Remove this requirement by making CuckooCache not require external locks
            if (validation_cache.HasScriptExecution(hashCacheEntry, !cacheFullScriptStore)) {
                return true;
            }

            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint &prevout = tx.vin[i].prevout;
                const Coin& coin = inputs.AccessCoin(prevout);
                assert(!coin.IsSpent());

                // We very carefully only pass in things to CScriptCheck which
                // are clearly committed to by tx's hash. This provides
                // a sanity check that our caching is not introducing consensus
                // failures through additional data in, eg, the coins being
                // spent being checked as a part of CScriptCheck.

                // Verify signature
                CScriptCheck check(coin.out, tx, i, flags, cacheSigStore, validation_cache.Signatures());
                if (pvChecks) {
                    pvChecks->emplace_back(std::move(check));
                } else if (const auto err = check()) {
                    // Flags that are in STANDARD but not in the mandatory block flags
                    // for this height are truly non-mandatory (policy-only).
                    // Flags present in mandatoryFlags are consensus-mandatory even if
                    // they also appear in PolicyConstants::STANDARD_SCRIPT_VERIFY_FLAGS (e.g.
                    // SCRIPT_VERIFY_CP2SH_COLORED after softfork activation).
                    const unsigned int nonMandatory = PolicyConstants::STANDARD_NOT_MANDATORY_VERIFY_FLAGS & ~mandatoryFlags;
                    if (flags & nonMandatory) {
                        // Check whether the failure was caused by a non-mandatory script
                        // verification flag. If check2 passes, the failure is non-mandatory
                        // → mempool rejects as non-standard without DoS.
                        // If check2 also fails, fall through to DoS.
                        CScriptCheck check2(coin.out, tx, i,
                                flags & ~nonMandatory, cacheSigStore, validation_cache.Signatures());
                        if (!check2().has_value()) {
                            LogPrint(BCLog::MEMPOOLREJ, "%s: tx %s input %u flags=0x%08x non-mandatory script failure: %s\n",
                                __func__, tx.GetHashMalFix().ToString(), i, flags, ScriptErrorString(*err));
                            return state.Invalid(false, REJECT_NONSTANDARD,
                                strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(*err)));
                        }
                    }
                    return state.DoS(100, error("%s: tx %s input %u flags=0x%08x script failure: %s",
                            __func__, tx.GetHashMalFix().ToString(), i, flags, ScriptErrorString(*err)),
                        REJECT_INVALID,
                        strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(*err)));
                }
            }

            if (cacheFullScriptStore && !pvChecks) {
                // We executed all of the provided scripts, and were told to
                // cache the result. Do so now.
                validation_cache.AddScriptExecution(hashCacheEntry);
            }
        }
    }

    return true;
}

void StartScriptCheckWorkerThreads(int threads_num)
{
    g_chainstate_manager.ActiveChainstate().scriptcheckqueue = std::make_unique< CCheckQueue<CScriptCheck> >(128, threads_num);
}

void FlushStateToDisk() {
    CValidationState state;
    if (!g_chainstate_manager.ActiveChainstate().FlushStateToDisk(state, FlushStateMode::ALWAYS)) {
        LogPrintf("%s: failed to flush state (%s)\n", __func__, FormatStateMessage(state));
    }
}

void PruneAndFlush() {
    CValidationState state;
    g_chainstate_manager.GetBlockFileManager().SetCheckForPruning();
    if (!g_chainstate_manager.ActiveChainstate().FlushStateToDisk(state, FlushStateMode::NONE)) {
        LogPrintf("%s: failed to flush state (%s)\n", __func__, FormatStateMessage(state));
    }
}

/* This function is called from the RPC code for pruneblockchain */
void PruneBlockFilesManual(int nManualPruneHeight)
{
    CValidationState state;
    if (!g_chainstate_manager.ActiveChainstate().FlushStateToDisk(state, FlushStateMode::NONE, nManualPruneHeight)) {
        LogPrintf("%s: failed to flush state (%s)\n", __func__, FormatStateMessage(state));
    }
}

bool ActivateBestChain(CValidationState &state, std::shared_ptr<const CBlock> pblock) {
    return g_chainstate_manager.ActiveChainstate().ActivateBestChain(state, std::move(pblock));
}

bool PreciousBlock(CValidationState& state, CBlockIndex *pindex) {
    return g_chainstate_manager.ActiveChainstate().PreciousBlock(state, pindex);
}

bool InvalidateBlock(CValidationState& state, CBlockIndex *pindex) {
    return g_chainstate_manager.ActiveChainstate().InvalidateBlock(state, pindex);
}

void ResetBlockFailureFlags(CBlockIndex *pindex) {
    return g_chainstate_manager.ActiveChainstate().ResetBlockFailureFlags(pindex);
}

// Exposed wrapper for AcceptBlockHeader
bool ProcessNewBlockHeaders(const std::vector<CBlockHeader>& headers, CValidationState& state, const CBlockIndex** ppindex, CBlockHeader *first_invalid)
{
    // copy of the active chain's xfield history, used until we finish processing this headers message
    CTempXFieldHistory tempFieldHistory(g_chainstate_manager.XFieldHistory());
    if (first_invalid != nullptr) first_invalid->SetNull();
    {
        LOCK(cs_main);
        for (const CBlockHeader& header : headers) {
            CBlockIndex *pindex = nullptr; // Use a temp pindex instead of ppindex to avoid a const_cast
            if (!g_chainstate_manager.ActiveChainstate().AcceptBlockHeader(header, state, &pindex, &tempFieldHistory)) {
                if (first_invalid) *first_invalid = header;
                return false;
            }
            if (ppindex) {
                *ppindex = pindex;
            }
        }
    }
    NotifyHeaderTip();
    return true;
}

bool ProcessNewBlock(const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool *fNewBlock)
{
    AssertLockNotHeld(cs_main);

    {
        CBlockIndex *pindex = nullptr;
        if (fNewBlock) *fNewBlock = false;
        CValidationState state;
        // Ensure that CheckBlock() passes before calling AcceptBlock, as
        // belt-and-suspenders.
        bool ret = CheckBlock(*pblock, state, g_chainstate_manager.RulesLatest());

        LOCK(cs_main);

        if (ret) {
            // Store to disk
            ret = g_chainstate_manager.ActiveChainstate().AcceptBlock(pblock, state, &pindex, fForceProcessing, nullptr, fNewBlock);
        }
        if (!ret) {
            GetMainSignals().BlockChecked(*pblock, state);
            return error("%s: AcceptBlock FAILED (%s)", __func__, FormatStateMessage(state));
        }
    }

    NotifyHeaderTip();

    CValidationState state; // Only used to report errors, not invalidity - ignore it
    if (!g_chainstate_manager.ActiveChainstate().ActivateBestChain(state, pblock))
        return error("%s: ActivateBestChain failed (%s)", __func__, FormatStateMessage(state));

    return true;
}

bool TestBlockValidity(CValidationState& state, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev && pindexPrev == g_chainstate_manager.ActiveChain().Tip());
    CCoinsViewCache viewNew(&g_chainstate_manager.ActiveChainstate().CoinsTip());
    uint256 block_hash(block.GetHash());
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;
    indexDummy.phashBlock = &block_hash;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualConsensusRules::ContextualCheckBlockHeader(block, state, *pindexPrev, GetAdjustedTime(), GetLastCheckpointIfEnabled()))
        return error("%s: Consensus::ContextualCheckBlockHeader: %s", __func__, FormatStateMessage(state));
    if (!CheckBlock(block, state, g_chainstate_manager.RulesAfter(*pindexPrev), fCheckPOW, fCheckMerkleRoot))
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));
    if (!ContextualConsensusRules::ContextualCheckBlock(block, state, *pindexPrev))
        return error("%s: Consensus::ContextualCheckBlock: %s", __func__, FormatStateMessage(state));
    if (!g_chainstate_manager.ActiveChainstate().ConnectBlock(block, state, &indexDummy, viewNew, g_chainstate_manager.IssuedColorIds(), true))
        return false;
    assert(state.IsValid());

    return true;
}

bool static LoadBlockIndexDB() EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    if (!g_chainstate_manager.ActiveChainstate().LoadBlockIndex())
        return false;

    if (!g_chainstate_manager.GetBlockFileManager().LoadBlockFileInfo())
        return false;
    g_chainstate_manager.LoadPersistentFlags();
    return true;
}

bool LoadChainTip()
{
    BlockIndexMap& blockIndex = g_chainstate_manager.BlockIndex();
    AssertLockHeld(cs_main);

    if (g_chainstate_manager.ActiveChain().Tip() && g_chainstate_manager.ActiveChain().Tip()->GetBlockHash() == g_chainstate_manager.ActiveChainstate().CoinsTip().GetBestBlock()) return true;

    if (g_chainstate_manager.ActiveChainstate().CoinsTip().GetBestBlock().IsNull() && blockIndex.mapBlockIndex.size() == 1) {
        // In case we just added the genesis block, connect it now, so
        // that we always have an active tip when we return.
        LogPrintf("%s: Connecting genesis block...\n", __func__);
        CValidationState state;
        if (!ActivateBestChain(state)) {
            LogPrintf("%s: failed to activate chain (%s)\n", __func__, FormatStateMessage(state));
            return false;
        }
    }

    // Load pointer to end of best chain
    CBlockIndex* pindex = blockIndex.LookupBlockIndex(g_chainstate_manager.ActiveChainstate().CoinsTip().GetBestBlock());
    if (!pindex) {
        return false;
    }
    g_chainstate_manager.ActiveChain().SetTip(pindex);

    g_chainstate_manager.ActiveChainstate().PruneBlockIndexCandidates();

    LogPrintf("Loaded best chain: hashBestChain=%s height=%d date=%s progress=%f\n",
        g_chainstate_manager.ActiveChain().Tip()->GetBlockHash().ToString(), g_chainstate_manager.ActiveChain().Height(),
        FormatISO8601DateTime(g_chainstate_manager.ActiveChain().Tip()->GetBlockTime()),
        g_chainstate_manager.Progress().Guess(g_chainstate_manager.ActiveChain().Tip()));
    return true;
}

bool ReplayBlocks(CCoinsView* view, CIssuedColorIds& issuedColorIds) {
    return g_chainstate_manager.ActiveChainstate().ReplayBlocks(view, issuedColorIds);
}

bool RewindBlockIndex() {
    if (!g_chainstate_manager.ActiveChainstate().RewindBlockIndex()) {
        return false;
    }

    if (g_chainstate_manager.ActiveChain().Tip() != nullptr) {
        // FlushStateToDisk can possibly read the active chain. Be conservative
        // and skip it here, we're about to -reindex-chainstate anyway, so
        // it'll get called a bunch real soon.
        CValidationState state;
        if (!g_chainstate_manager.ActiveChainstate().FlushStateToDisk(state, FlushStateMode::ALWAYS)) {
            LogPrintf("RewindBlockIndex: unable to flush state to disk (%s)\n", FormatStateMessage(state));
            return false;
        }
    }

    return true;
}

// May NOT be used after any connections are up as much
// of the peer-processing logic assumes a consistent
// block index state
void UnloadBlockIndex(CTxMemPool* mempool)
{
    LOCK(cs_main);
    g_chainstate_manager.ActiveChain().SetTip(nullptr);
    g_chainstate_manager.BestInvalid() = nullptr;
    g_chainstate_manager.BestHeader() = nullptr;
    if (mempool) mempool->clear();
    g_chainstate_manager.BlockIndex().Unload();
    g_chainstate_manager.GetBlockFileManager().Unload();
    g_chainstate_manager.UnloadPersistentFlags();

    g_chainstate_manager.ActiveChainstate().UnloadBlockIndex();
}

bool LoadBlockIndex()
{
    BlockIndexMap& blockIndex = g_chainstate_manager.BlockIndex();
    // Load block index from databases
    bool needs_init = g_chainstate_manager.IsReindexing();
    if (!needs_init) {
        bool ret = LoadBlockIndexDB();
        if (!ret) return false;
        needs_init = blockIndex.mapBlockIndex.empty();
    }

    if (needs_init) {
        // Everything here is for *new* reindex/DBs. Thus, though
        // LoadBlockIndexDB may have set the reindexing flag if we shut down
        // mid-reindex previously, we don't check it and
        // instead only check it prior to LoadBlockIndexDB to set
        // needs_init.

        LogPrintf("Initializing databases...\n");
    }
    return true;
}

bool LoadGenesisBlock()
{
    return g_chainstate_manager.ActiveChainstate().LoadGenesisBlock();
}
std::string CBlockFileInfo::ToString() const
{
    AssertLockHeld(::cs_main);
    return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, FormatISO8601Date(nTimeFirst), FormatISO8601Date(nTimeLast));
}

