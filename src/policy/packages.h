// Copyright (c) 2021-2022 The Bitcoin Core developers
// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_POLICY_PACKAGES_H
#define TAPYRUS_POLICY_PACKAGES_H

#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <validation.h>
#include <cstdint>
#include <vector>

/** Default maximum number of transactions in a package. */
static constexpr uint32_t MAX_PACKAGE_COUNT{25};

// If a package is submitted, it must be within the mempool's ancestor/descendant limits. Since a
// submitted package must be child-with-unconfirmed-parents (all of the transactions are an ancestor
// of the child), package limits are ultimately bounded by mempool package limits. Ensure that the
// defaults reflect this constraint.
static_assert(DEFAULT_DESCENDANT_LIMIT >= MAX_PACKAGE_COUNT);
static_assert(DEFAULT_ANCESTOR_LIMIT >= MAX_PACKAGE_COUNT);

/** A package is an set of transactions. The transactions cannot conflict with (spend the
 * same inputs as) one another. */
using Package = std::vector<CTransactionRef>;

using PackageValidationState = std::map<const uint256, const CValidationState >;
/** Context-free package policy checks:
 * 1. The number of transactions cannot exceed MAX_PACKAGE_COUNT.
 * 2. The total size cannot exceed  MAX_PACKAGE_COUNT * 1000
 * 3. If any dependencies exist between transactions, parents must appear before children.
 * 4. Transactions cannot conflict, i.e., spend the same inputs.
 */
bool CheckPackage(const Package& txns, CValidationState& state);

void FilterMempoolDuplicates(const std::vector<CTransaction>& txns, Package& package, PackageValidationState& results);

bool SubmitPackageToMempool(const Package& package,
                                  CValidationState& state,
                                  PackageValidationState& results,
                                  CTxMempoolAcceptanceOptions& opt);

bool ArePackageTransactionsAccepted(const PackageValidationState& results);

#endif // TAPYRUS_POLICY_PACKAGES_H
