// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <transactioncolorchecker.h>

#include <consensus/validation.h>
#include <precomputedcolordata.h>

#include <cstdint>
#include <map>

bool TransactionColorChecker::CheckOutputColorIds(CValidationState& state) const
{
    // Each NFT colorid must appear on exactly one output.
    std::map<ColorIdentifier, uint32_t> nftOutputCount;

    for (const PrecomputedColorData::Output& out : m_data.Outputs()) {
        if (!out.colored)
            continue;

        if (out.colorId.type == TokenTypes::NONE) {
            if (m_data.Cp2shColoredActive())
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-nonstandard-opcolor");
            continue;
        }

        if (out.value <= 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-value");

        if (out.colorId.type == TokenTypes::NFT)
            nftOutputCount[out.colorId]++;

        // The first unspent input the colorid derives from: issued from a TPC
        // input, or transferred from an input of the same colorid.
        bool matchFound = false;
        bool isIssuance = false;
        for (const PrecomputedColorData::Input& in : m_data.Inputs()) {
            if (in.nonstandardColored) {
                if (m_data.Cp2shColoredActive())
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-nonstandard-opcolor");
                continue;
            }

            ColorIdentifier inColorId;
            switch (in.colorId.type) {
                case TokenTypes::NONE:
                    if (out.colorId.type == TokenTypes::REISSUABLE) {
                        inColorId = in.scriptColorId;
                    } else {
                        inColorId = in.outpointColorId;
                        inColorId.type = out.colorId.type;
                    }
                    break;
                case TokenTypes::REISSUABLE:
                case TokenTypes::NON_REISSUABLE:
                case TokenTypes::NFT:
                    inColorId = in.colorId;
                    break;
                default:
                    continue;
            }

            if (inColorId == out.colorId && !in.spent) {
                matchFound = true;
                isIssuance = in.colorId.type == TokenTypes::NONE;
                break;
            }
        }

        if (!matchFound)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-noinput");

        if (out.colorId.type == TokenTypes::NFT && out.value != 1)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-nft-amount");

        // A non-reissuable or NFT colorid is issued once; several outputs of one
        // non-reissuable issuance split its supply.
        if (isIssuance && out.issued)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-colorid-already-issued");
    }

    for (const auto& entry : nftOutputCount) {
        if (entry.second != 1)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-nft-output-count");
    }
    return true;
}

bool TransactionColorChecker::CheckTokenBalances(CValidationState& state, CAmount minFee) const
{
    TxColoredCoinBalancesMap inBalances;
    for (const PrecomputedColorData::Input& in : m_data.Inputs()) {
        // Before the softfork a non-standard OP_COLOR input counts as TPC.
        if (in.nonstandardColored && m_data.Cp2shColoredActive())
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-nonstandard-opcolor");
        auto it = inBalances.find(in.colorId);
        if (it == inBalances.end())
            it = inBalances.emplace(in.colorId, CAmount{0}).first;
        // A reissuable token has no supply cap, so bound each value and the
        // running sum per colorid to keep the sum from overflowing.
        if (!MoneyRange(in.value) || !MoneyRange(it->second + in.value))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        it->second += in.value;
    }

    TxColoredCoinBalancesMap outBalances;
    for (const PrecomputedColorData::Output& out : m_data.Outputs()) {
        auto it = outBalances.find(out.colorId);
        if (it == outBalances.end())
            outBalances.emplace(out.colorId, out.value);
        else
            it->second += out.value;
    }

    CAmount tpcIn{0};
    CAmount tpcOut{0};
    auto tpcInIt = inBalances.find(ColorIdentifier());
    if (tpcInIt != inBalances.end())
        tpcIn = tpcInIt->second;
    auto tpcOutIt = outBalances.find(ColorIdentifier());
    if (tpcOutIt != outBalances.end())
        tpcOut = tpcOutIt->second;

    if (tpcIn <= 0)
        return state.Invalid(false, REJECT_INSUFFICIENTFEE, "bad-txns-token-without-fee");

    for (const auto& out : outBalances) {
        auto in = inBalances.find(out.first);
        if (in == inBalances.end()) {
            // A colorid with no input is an issuance, paid for in TPC.
            if (tpcIn < 0 || tpcIn - tpcOut - minFee < 0)
                return state.Invalid(false, REJECT_INSUFFICIENTFEE, "bad-txns-token-insufficient");
        } else if (out.second > in->second) {
            return state.Invalid(false, REJECT_INVALID, "bad-txns-token-balance");
        } else {
            in->second -= out.second;
        }
    }
    return true;
}

void TransactionColorChecker::GetNewIssuances(std::set<ColorIdentifier>& issuances) const
{
    for (const PrecomputedColorData::Output& out : m_data.Outputs()) {
        if (out.colorId.type != TokenTypes::NON_REISSUABLE && out.colorId.type != TokenTypes::NFT)
            continue;
        for (const PrecomputedColorData::Input& in : m_data.Inputs()) {
            if (in.colorId.type != TokenTypes::NONE)
                continue;
            ColorIdentifier inColorId = in.outpointColorId;
            inColorId.type = out.colorId.type;
            if (inColorId == out.colorId) {
                issuances.insert(out.colorId);
                break;
            }
        }
    }
}
