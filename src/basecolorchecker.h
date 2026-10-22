// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_BASECOLORCHECKER_H
#define TAPYRUS_BASECOLORCHECKER_H

#include <amount.h>

class CValidationState;

/**
 * Colour rules of one transaction, in the style of BaseSignatureChecker.
 * A failed check fills the state with the DoS score, reject code and reason.
 */
class BaseColorChecker
{
public:
    /** Every colored output is well formed and derives from an input. */
    virtual bool CheckOutputColorIds(CValidationState& state) const
    {
        return false;
    }

    /** No token value comes from nothing; an issuance pays at least minFee in TPC. */
    virtual bool CheckTokenBalances(CValidationState& state, CAmount minFee) const
    {
        return false;
    }

    virtual ~BaseColorChecker() {}
};

#endif // TAPYRUS_BASECOLORCHECKER_H
