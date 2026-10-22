// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_TRANSACTIONCOLORCHECKER_H
#define TAPYRUS_TRANSACTIONCOLORCHECKER_H

#include <basecolorchecker.h>
#include <coloridentifier.h>

#include <set>

class PrecomputedColorData;

/** Checks one transaction's colour rules from its PrecomputedColorData. */
class TransactionColorChecker : public BaseColorChecker
{
public:
    explicit TransactionColorChecker(const PrecomputedColorData& data) : m_data(data) {}

    bool CheckOutputColorIds(CValidationState& state) const override;
    bool CheckTokenBalances(CValidationState& state, CAmount minFee) const override;

    /** Add the non-reissuable and NFT colorids this transaction issues from its TPC inputs. */
    void GetNewIssuances(std::set<ColorIdentifier>& issuances) const;

private:
    const PrecomputedColorData& m_data;
};

#endif // TAPYRUS_TRANSACTIONCOLORCHECKER_H
