// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <precomputedcolordata.h>

#include <coinsviewcache.h>
#include <issuedcolorids.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <softforkmanager.h>

PrecomputedColorData::PrecomputedColorData(const CTransaction& tx, const CCoinsViewCache& inputs, int32_t blockHeight)
    : m_coinbase(tx.IsCoinBase()),
      m_cp2sh_colored_active(GetSoftForkManager().IsActive(SCRIPT_VERIFY_CP2SH_COLORED, blockHeight))
{
    // Outputs first, so the inputs hash only the colorids some output can issue.
    bool needScriptColorId{false};
    bool needOutpointColorId{false};
    m_outputs.reserve(tx.vout.size());
    for (const CTxOut& txout : tx.vout) {
        Output out;
        out.value = txout.nValue;
        out.colorId = GetColorIdFromScript(txout.scriptPubKey);
        out.colored = txout.scriptPubKey.IsColoredScript();
        if (out.colorId.type == TokenTypes::REISSUABLE)
            needScriptColorId = true;
        else if (out.colorId.type != TokenTypes::NONE)
            needOutpointColorId = true;
        m_output_balances[out.colorId] += out.value;
        m_outputs.push_back(std::move(out));
    }

    m_inputs.reserve(tx.vin.size());
    for (const CTxIn& txin : tx.vin) {
        const Coin& coin = inputs.AccessCoin(txin.prevout);
        Input in;
        in.colorId = GetColorIdFromScript(coin.out.scriptPubKey);
        in.nonstandardColored = in.colorId.type == TokenTypes::NONE && coin.out.scriptPubKey.IsColoredScript();
        in.spent = coin.IsSpent();
        // Hash once per input instead of once per (output, input) pair, and
        // only when an output carries a colorid of that kind.
        if (in.colorId.type == TokenTypes::NONE) {
            if (needOutpointColorId)
                in.outpointColorId = ColorIdentifier(txin.prevout, TokenTypes::NON_REISSUABLE);
            if (needScriptColorId && !in.nonstandardColored)
                in.scriptColorId = ColorIdentifier(coin.out.scriptPubKey);
        }
        // A reissuable token has no supply cap, so bound each value and the
        // running sum per colorid to keep the sum from overflowing.
        if (!m_first_out_of_range_input) {
            CAmount& sum = m_input_balances[in.colorId];
            const CAmount value{coin.out.nValue};
            if (MoneyRange(value) && MoneyRange(sum + value))
                sum += value;
            else
                m_first_out_of_range_input = m_inputs.size();
        }
        m_inputs.push_back(std::move(in));
    }

}

void PrecomputedColorData::SetIssued(const CIssuedColorIds& issued)
{
    for (Output& out : m_outputs) {
        if (out.colorId.type == TokenTypes::NON_REISSUABLE || out.colorId.type == TokenTypes::NFT)
            out.issued = issued.IsIssued(out.colorId);
    }
}

CAmount PrecomputedColorData::TpcIn() const
{
    const auto it = m_input_balances.find(ColorIdentifier());
    return it == m_input_balances.end() ? CAmount{0} : it->second;
}

CAmount PrecomputedColorData::TpcOut() const
{
    const auto it = m_output_balances.find(ColorIdentifier());
    return it == m_output_balances.end() ? CAmount{0} : it->second;
}
