// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_TRANSACTIONCOLORCHECKER_H
#define TAPYRUS_TRANSACTIONCOLORCHECKER_H

#include <amount.h>
#include <coloridentifier.h>

#include <set>

class CValidationState;
class PrecomputedColorData;

/**
 * Checks one transaction's colour rules from its PrecomputedColorData.
 * A failed check fills the state with the DoS score, reject code and reason.
 */
class TransactionColorChecker
{
public:
    explicit TransactionColorChecker(const PrecomputedColorData& data) : m_data(data) {}

    /** Every colored output is well formed and derives from an input. */
    bool CheckOutputColorIds(CValidationState& state) const;

    /** No token value comes from nothing; an issuance pays at least minFee in TPC. */
    bool CheckTokenBalances(CValidationState& state, CAmount minFee) const;

    /** Add the non-reissuable and NFT colorids this transaction issues from its TPC inputs. */
    void GetNewIssuances(std::set<ColorIdentifier>& issuances) const;

private:
    const PrecomputedColorData& m_data;
};

#endif // TAPYRUS_TRANSACTIONCOLORCHECKER_H
