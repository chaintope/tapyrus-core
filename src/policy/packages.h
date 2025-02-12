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

/**
 * FilterMempoolDuplicates creates a package from the given list of transactions
 * after filetring those that already exist in the mempool.
 *
 * @param txns A list of transactions to check for duplicates.
 * @param package The package of transactions after filtering.
 * @param results The package validation state where results are recorded.
 */
void FilterMempoolDuplicates(const std::vector<CTransaction>& txns, Package& package, PackageValidationState& results);

/**
 * SubmitPackageToMempool is used to submit the package to mempool after validation checks.
 * It gives granular results with the state of each transaction in the package and
 * the state of the package as a whole.
 *
 * @param package The package of transactions to be submitted.
 * @param state A reference to the validation state that tracks the package validation results.
 * @param results A reference to the package validation state that records the validation outcome of each transaction.
 * @param opt Options that control how the mempool accepts transactions.
 * @return True if the package is successfully accepted into the mempool, false otherwise.
 */
bool SubmitPackageToMempool(const Package& package,
                                  CValidationState& state,
                                  PackageValidationState& results,
                                  CTxMempoolAcceptanceOptions& opt);

/**
 * ArePackageTransactionsAccepted checks the result of a package submit attempt and
 * tells whether all the transactions in the package were accepted.
 * @param results The validation state of the package(this is the output from submitpackage or testpackage acceptance).
 * @return True if accepted, false otherwise.
 */
bool ArePackageTransactionsAccepted(const PackageValidationState& results);

#endif // TAPYRUS_POLICY_PACKAGES_H
