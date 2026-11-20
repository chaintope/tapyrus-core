// Copyright (c) 2021-2023 The Bitcoin Core developers
// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/packages.h>

#include <hasher.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <util.h>

#include <algorithm>
#include <numeric>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
/** The largest in-package ancestor and descendant counts of any transaction in
 * a sorted, duplicate-free package, each counting the transaction itself as
 * the mempool limits do. */
class PackageChainCounts
{
public:
    explicit PackageChainCounts(const Package& txns)
    {
        std::unordered_map<uint256, size_t, SaltedTxidHasher> index;
        std::vector<std::set<size_t>> ancestors(txns.size());
        for (size_t i{0}; i < txns.size(); ++i) {
            for (const auto& input : txns[i]->vin) {
                const auto parent = index.find(input.prevout.hashMalFix);
                if (parent == index.end()) continue;
                ancestors[i].insert(parent->second);
                ancestors[i].insert(ancestors[parent->second].begin(), ancestors[parent->second].end());
            }
            index.emplace(txns[i]->GetHashMalFix(), i);
        }
        std::vector<size_t> descendants(txns.size(), size_t{1});
        for (size_t i{0}; i < txns.size(); ++i) {
            m_max_ancestors = std::max(m_max_ancestors, ancestors[i].size() + 1);
            for (const size_t ancestor : ancestors[i]) ++descendants[ancestor];
        }
        for (const size_t count : descendants) m_max_descendants = std::max(m_max_descendants, count);
    }

    size_t MaxAncestors() const { return m_max_ancestors; }
    size_t MaxDescendants() const { return m_max_descendants; }

private:
    size_t m_max_ancestors{0};
    size_t m_max_descendants{0};
};
} // namespace

bool CheckPackage(const Package& txns, CValidationState& state)
{
    const unsigned int package_count = txns.size();

    if (package_count > g_policy_settings->maxPackageCount) {
        return state.Invalid(false, REJECT_PACKAGE_INVALID, "package-too-many-transactions");
    }

    const int64_t total_size = std::accumulate(txns.cbegin(), txns.cend(), 0,
                               [](int64_t sum, const auto& tx) { return sum + GetTransactionSize(*tx); });

    // If the package only contains 1 tx, it's better to report the policy violation on individual tx size.
    if (package_count > 1 && total_size > (int64_t)g_policy_settings->maxPackageCount * 1000) {
        return state.Invalid(false, REJECT_PACKAGE_INVALID, "package-too-large");
    }

    // Require the package to be sorted in order of dependency, i.e. parents appear before children.
    // An unsorted package will fail anyway on missing-inputs, but it's better to quit earlier and
    // fail on something less ambiguous (missing-inputs could also be an orphan or trying to
    // spend nonexistent coins).
    std::unordered_set<uint256, SaltedTxidHasher> later_txids;
    std::transform(txns.cbegin(), txns.cend(), std::inserter(later_txids, later_txids.end()),
                   [](const auto& tx) { return tx->GetHashMalFix(); });
    for (const auto& tx : txns) {
        for (const auto& input : tx->vin) {
            if (later_txids.find(input.prevout.hashMalFix) != later_txids.end()) {
                // The parent is a subsequent transaction in the package.
                return state.Invalid(false, REJECT_PACKAGE_INVALID, "package-not-sorted");
            }
        }
        later_txids.erase(tx->GetHashMalFix());
    }

    // Don't allow any conflicting transactions, i.e. spending the same inputs, in a package.
    std::unordered_set<COutPoint, SaltedOutpointHasher> inputs_seen;
    for (const auto& tx : txns) {
        for (const auto& input : tx->vin) {
            if (inputs_seen.find(input.prevout) != inputs_seen.end()) {
                // This input is also present in another tx in the package.
                return state.Invalid(false, REJECT_PACKAGE_INVALID, "conflict-in-package");
            }
        }
        // Batch-add all the inputs for a tx at a time. If we added them 1 at a time, we could
        // catch duplicate inputs within a single tx.  This is a more severe, consensus error,
        // and we want to report that from CheckTransaction instead.
        std::transform(tx->vin.cbegin(), tx->vin.cend(), std::inserter(inputs_seen, inputs_seen.end()),
                       [](const auto& input) { return input.prevout; });
    }

    // Package must not contain any duplicate transactions, which is checked by txid.
    later_txids.clear();
    std::transform(txns.cbegin(), txns.cend(), std::inserter(later_txids, later_txids.end()),
                   [](const auto& tx) { return tx->GetHashMalFix(); });

    if (later_txids.size() != txns.size()) {
        return state.Invalid(false, REJECT_PACKAGE_INVALID, "package-contains-duplicates");
    }

    // Only chains inside the package are counted here; ancestors already in the
    // mempool are counted when each transaction is accepted.
    const PackageChainCounts chain_counts(txns);
    const int64_t ancestor_limit{g_policy_settings->ancestorLimit};
    const int64_t descendant_limit{g_policy_settings->descendantLimit};
    if (static_cast<int64_t>(chain_counts.MaxAncestors()) > ancestor_limit ||
        static_cast<int64_t>(chain_counts.MaxDescendants()) > descendant_limit) {
        return state.Invalid(false, REJECT_PACKAGE_INVALID, "package-mempool-limits");
    }

    return true;
}

bool ArePackageTransactionsAccepted(const PackageValidationState& results)
{
    for (const auto& r : results) {
        if(r.second.IsInvalid() || r.second.IsError() || r.second.missingInputs) {
            return false;
        }
    }
    return true;
}

