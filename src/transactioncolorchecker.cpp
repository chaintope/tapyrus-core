// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <transactioncolorchecker.h>

#include <consensus/validation.h>
#include <precomputedcolordata.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

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
    const std::vector<PrecomputedColorData::Input>& inputs = m_data.Inputs();
    const std::optional<size_t> outOfRange = m_data.FirstOutOfRangeInput();
    for (size_t i = 0; i < inputs.size(); ++i) {
        // Before the softfork a non-standard OP_COLOR input counts as TPC.
        if (inputs[i].nonstandardColored && m_data.Cp2shColoredActive())
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-nonstandard-opcolor");
        if (outOfRange == i)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
    }

    const CAmount tpcIn = m_data.TpcIn();
    const CAmount tpcOut = m_data.TpcOut();
    if (tpcIn <= 0)
        return state.Invalid(false, REJECT_INSUFFICIENTFEE, "bad-txns-token-without-fee");

    const TxColoredCoinBalancesMap& inBalances = m_data.InputBalances();
    for (const auto& out : m_data.OutputBalances()) {
        const auto in = inBalances.find(out.first);
        if (in == inBalances.end()) {
            // A colorid with no input is an issuance, paid for in TPC.
            if (tpcIn < 0 || tpcIn - tpcOut - minFee < 0)
                return state.Invalid(false, REJECT_INSUFFICIENTFEE, "bad-txns-token-insufficient");
        } else if (out.second > in->second) {
            return state.Invalid(false, REJECT_INVALID, "bad-txns-token-balance");
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
