// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <script/coloredsignaturechecker.h>

bool ColoredSignatureChecker::CheckColorIdentifier(const ColorIdentifier& colorId) const
{
    // Before the CP2SH colored softfork a non-standard OP_COLOR coin is
    // spendable and has no precomputed colorid, so it is not compared.
    if (m_spent_color_id.type == TokenTypes::NONE)
        return true;
    return colorId == m_spent_color_id;
}
