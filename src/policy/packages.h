// Copyright (c) 2021-2022 The Bitcoin Core developers
// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_POLICY_PACKAGES_H
#define TAPYRUS_POLICY_PACKAGES_H

#include <consensusconstants.h>
#include <consensus/validation.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <cstdint>
#include <vector>

/** A package is an set of transactions. The transactions cannot conflict with (spend the
 * same inputs as) one another. */
using Package = std::vector<CTransactionRef>;

using PackageValidationState = std::map<const uint256, const CValidationState >;

/** Package policy checks that need no chain state:
 * 1. The number of transactions cannot exceed -maxpackagecount.
 * 2. The total size cannot exceed -maxpackagecount * 1000. This is deliberately independent of
 *    the ancestor and descendant limits, which bound chain length, not size.
 * 3. If any dependencies exist between transactions, parents must appear before children.
 * 4. Transactions cannot conflict, i.e., spend the same inputs.
 * 5. Transactions cannot be duplicated.
 * 6. No transaction can have more in-package ancestors or descendants, itself included, than
 *    -limitancestorcount or -limitdescendantcount. Unrelated transactions are not limited.
 */
bool CheckPackage(const Package& txns, CValidationState& state);

/**
 * ArePackageTransactionsAccepted checks the result of a package submit attempt and
 * tells whether all the transactions in the package were accepted.
 * @param results The validation state of the package(this is the output from submitpackage or testpackage acceptance).
 * @return True if accepted, false otherwise.
 */
bool ArePackageTransactionsAccepted(const PackageValidationState& results);

#endif // TAPYRUS_POLICY_PACKAGES_H
