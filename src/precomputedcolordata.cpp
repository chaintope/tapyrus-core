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
    m_inputs.reserve(tx.vin.size());
    for (const CTxIn& txin : tx.vin) {
        const Coin& coin = inputs.AccessCoin(txin.prevout);
        Input in;
        in.prevout = txin.prevout;
        in.value = coin.out.nValue;
        in.colorId = GetColorIdFromScript(coin.out.scriptPubKey);
        in.nonstandardColored = in.colorId.type == TokenTypes::NONE && coin.out.scriptPubKey.IsColoredScript();
        in.spent = coin.IsSpent();
        // Hash once per input instead of once per (output, input) pair.
        if (in.colorId.type == TokenTypes::NONE) {
            in.outpointColorId = ColorIdentifier(txin.prevout, TokenTypes::NON_REISSUABLE);
            if (!in.nonstandardColored)
                in.scriptColorId = ColorIdentifier(coin.out.scriptPubKey);
        }
        m_inputs.push_back(std::move(in));
    }

    m_outputs.reserve(tx.vout.size());
    for (const CTxOut& txout : tx.vout) {
        Output out;
        out.value = txout.nValue;
        out.colorId = GetColorIdFromScript(txout.scriptPubKey);
        out.colored = txout.scriptPubKey.IsColoredScript();
        m_outputs.push_back(std::move(out));
    }
}

void PrecomputedColorData::SetIssued(const CIssuedColorIds& issued)
{
    for (Output& out : m_outputs) {
        if (out.colorId.type == TokenTypes::NON_REISSUABLE || out.colorId.type == TokenTypes::NFT)
            out.issued = issued.IsIssued(out.colorId);
    }
}
