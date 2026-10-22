// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_SCRIPT_COLOREDSIGNATURECHECKER_H
#define TAPYRUS_SCRIPT_COLOREDSIGNATURECHECKER_H

#include <coloridentifier.h>
#include <script/sigcache.h>

/**
 * A caching signature checker that also compares the colorid OP_COLOR reads
 * with the spent coin's precomputed colorid. A consistency check: both come
 * from the same scriptPubKey.
 */
class ColoredSignatureChecker : public CachingTransactionSignatureChecker
{
public:
    ColoredSignatureChecker(const CTransaction* txToIn, unsigned int nInIn, const CAmount& amountIn, bool storeIn, const ColorIdentifier& spentColorId)
        : CachingTransactionSignatureChecker(txToIn, nInIn, amountIn, storeIn), m_spent_color_id(spentColorId) {}

    bool CheckColorIdentifier(const ColorIdentifier& colorId) const override;

private:
    //! NONE when the coin has no CP2PKH or CP2SH colorid; then nothing is compared
    const ColorIdentifier m_spent_color_id;
};

#endif // TAPYRUS_SCRIPT_COLOREDSIGNATURECHECKER_H
