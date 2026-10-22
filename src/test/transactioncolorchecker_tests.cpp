// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <coin.h>
#include <coinsview.h>
#include <coinsviewcache.h>
#include <coloridentifier.h>
#include <consensus/validation.h>
#include <cs_main.h>
#include <hash.h>
#include <issuedcolorids.h>
#include <precomputedcolordata.h>
#include <primitives/transaction.h>
#include <script/coloredsignaturechecker.h>
#include <script/interpreter.h>
#include <script/script_error.h>
#include <script/script.h>
#include <softforkmanager.h>
#include <test/test_tapyrus.h>
#include <transactioncolorchecker.h>
#include <validation.h>

#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {

// The colour checks as they were before TransactionColorChecker, kept
// unchanged as the reference for the differential test below.
bool LegacyCheckColorIdentifierValidity(const CTransaction& tx, CValidationState& state, CCoinsViewCache &inputs, int32_t blockHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
bool LegacyVerifyTokenBalances(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, CAmount minrelayFee, std::set<ColorIdentifier>* newIssuances, int32_t blockHeight);

bool LegacyCheckColorIdentifierValidity(const CTransaction& tx, CValidationState& state, CCoinsViewCache &inputs, int32_t blockHeight)
{
    // Track NFT colorId output count across the whole tx (must be exactly 1).
    std::map<ColorIdentifier, unsigned int> nftOutputCount;

    for(const auto& txout : tx.vout)
    {
        if(!txout.scriptPubKey.IsColoredScript())
            continue;

        ColorIdentifier outColorId(GetColorIdFromScript(txout.scriptPubKey));

        // outColorId is NONE: IsColoredPayToPubkeyHash / IsColoredPayToScriptHash
        // check both structure and type byte; reaching here means neither matched.
        if (outColorId.type == TokenTypes::NONE) {
            if (GetSoftForkManager().IsActive(SCRIPT_VERIFY_CP2SH_COLORED, blockHeight))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-nonstandard-opcolor");
            continue;
        }

        // Zero or negative token values are not allowed.
        if(txout.nValue <= 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-value");

        if(outColorId.type == TokenTypes::NFT)
            nftOutputCount[outColorId]++;

        bool matchFound = false;
        bool isIssuance = false;

        for(auto txin : tx.vin)
        {
            const Coin& coin = inputs.AccessCoin(txin.prevout);
            ColorIdentifier coinColorId;
            ColorIdentifier cid = GetColorIdFromScript(coin.out.scriptPubKey);

            if (cid.type == TokenTypes::NONE && coin.out.scriptPubKey.IsColoredScript()) {
                if (GetSoftForkManager().IsActive(SCRIPT_VERIFY_CP2SH_COLORED, blockHeight))
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-nonstandard-opcolor");
                continue;
            }

            switch(cid.type)
            {
                case TokenTypes::NONE:
                    // TPC input — this is a token issuance.
                    if(outColorId.type == TokenTypes::REISSUABLE)
                        coinColorId = ColorIdentifier(coin.out.scriptPubKey);
                    else {
                        COutPoint prevout = txin.prevout;
                        coinColorId = ColorIdentifier(prevout, outColorId.type);
                    }
                    break;

                case TokenTypes::REISSUABLE:
                case TokenTypes::NON_REISSUABLE:
                case TokenTypes::NFT:
                    coinColorId = GetColorIdFromScript(coin.out.scriptPubKey);
                    break;

                default:
                    continue;
            }

            if(coinColorId == outColorId && !coin.IsSpent())
            {
                matchFound = true;
                isIssuance = (cid.type == TokenTypes::NONE);
                break;
            }
        }

        if(!matchFound)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-token-noinput");

        // NFT value must always be exactly 1.
        if(outColorId.type == TokenTypes::NFT && txout.nValue != 1)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-nft-amount");

        // Global uniqueness: a NON_REISSUABLE or NFT colorId must never have been issued before.
        // (Multiple outputs with the same NON_REISSUABLE colorId in one tx are allowed —
        //  they split the initial supply. Re-issuance in a future block is impossible because
        //  the defining UTXO is consumed and its colorId = Hash(prevout) is globally unique.)
        if(isIssuance && (outColorId.type == TokenTypes::NON_REISSUABLE || outColorId.type == TokenTypes::NFT))
        {
            if(g_colorid_state && g_colorid_state->IsIssued(outColorId))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-colorid-already-issued");
        }
    }

    // NFT: exactly one output per colorId across the entire transaction.
    for(const auto& entry : nftOutputCount)
    {
        if(entry.second != 1)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-nft-output-count");
    }
    return true;
}

bool LegacyVerifyTokenBalances(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, CAmount minrelayFee, std::set<ColorIdentifier>* newIssuances, int32_t blockHeight)
{
    // Build input balance map from the UTXO view.
    // Simultaneously collect TPC input outpoints for issuance detection.
    TxColoredCoinBalancesMap inColoredCoinBalances;
    std::vector<COutPoint> tpcInputOutpoints;
    for (const auto& txin : tx.vin) {
        const Coin& coin = inputs.AccessCoin(txin.prevout);
        ColorIdentifier cid = GetColorIdFromScript(coin.out.scriptPubKey);
        // A legacy non-standard OP_COLOR UTXO (colorId resolves to NONE) is rejected
        // post-activation.  Pre-activation its nValue falls through to the TPC bucket.
        if (cid.type == TokenTypes::NONE && coin.out.scriptPubKey.IsColoredScript() &&
            GetSoftForkManager().IsActive(SCRIPT_VERIFY_CP2SH_COLORED, blockHeight))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-nonstandard-opcolor");
        auto it = inColoredCoinBalances.find(cid);
        if (it == inColoredCoinBalances.end())
            it = inColoredCoinBalances.emplace(cid, CAmount{0}).first;
        // A reissuable token has no supply cap, so bound each value and the
        // running sum per colorId; both stay within MAX_MONEY, so the sum
        // cannot overflow.
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(it->second + coin.out.nValue))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        it->second += coin.out.nValue;
        if (newIssuances && cid.type == TokenTypes::NONE)
            tpcInputOutpoints.push_back(txin.prevout);
    }

    //for every output eliminate a matching input.
    //verify that all outputs are matched
    TxColoredCoinBalancesMap outColoredCoinBalances;
    for (const auto& tx_out : tx.vout) {
        ColorIdentifier outColorId(GetColorIdFromScript(tx_out.scriptPubKey));

        //collect token balances from all outputs.
        auto iter = outColoredCoinBalances.find(outColorId);
        if(iter == outColoredCoinBalances.end())
            outColoredCoinBalances.emplace(outColorId, tx_out.nValue);
        else
            iter->second += tx_out.nValue;
    }
    // Tally transaction fees
    CAmount tpcin = 0, tpcout = 0;
    TxColoredCoinBalancesMap::const_iterator iter = inColoredCoinBalances.find(ColorIdentifier());
    if(iter != inColoredCoinBalances.end())
        tpcin = iter->second;

    iter = outColoredCoinBalances.find(ColorIdentifier());
        if(iter != outColoredCoinBalances.end())
            tpcout = iter->second;

    if(tpcin <= 0)
        return state.Invalid(false, REJECT_INSUFFICIENTFEE, "bad-txns-token-without-fee");

    for(auto& out:outColoredCoinBalances)
    {
        TxColoredCoinBalancesMap::iterator iter = inColoredCoinBalances.find(out.first);

        //output does not have a corresponding input.
        if(iter == inColoredCoinBalances.end())
        {
            //if TPC input is sufficiently large this is a token issue. 
            if(tpcin < 0 || tpcin - tpcout - minrelayFee< 0)
                return state.Invalid(false, REJECT_INSUFFICIENTFEE, "bad-txns-token-insufficient");
        }
        //output value is more than input
        else if(out.second > iter->second)
        {
            return state.Invalid(false, REJECT_INVALID, "bad-txns-token-balance");
        }
        else
        {
            //reduce this output from the input
            iter->second -=  out.second;
        }
    }

    // Detect issuances: a NON_REISSUABLE or NFT output whose colorId was
    // derived from one of the TPC inputs in this transaction.
    if (newIssuances) {
        for (const auto& tx_out : tx.vout) {
            ColorIdentifier outColorId(GetColorIdFromScript(tx_out.scriptPubKey));
            if (outColorId.type != TokenTypes::NON_REISSUABLE && outColorId.type != TokenTypes::NFT)
                continue;
            for (const auto& prevout : tpcInputOutpoints) {
                if (ColorIdentifier(prevout, outColorId.type) == outColorId) {
                    newIssuances->insert(outColorId);
                    break;
                }
            }
        }
    }

    return true;
}

CScript TpcScript(uint32_t n)
{
    std::vector<unsigned char> keyHash(20, static_cast<unsigned char>(n));
    return CScript() << OP_DUP << OP_HASH160 << keyHash << OP_EQUALVERIFY << OP_CHECKSIG;
}

CScript Cp2pkhScript(const ColorIdentifier& colorId)
{
    std::vector<unsigned char> keyHash(20, static_cast<unsigned char>(0x11));
    return CScript() << colorId.toVector() << OP_COLOR << OP_DUP << OP_HASH160 << keyHash << OP_EQUALVERIFY << OP_CHECKSIG;
}

CScript Cp2shScript(const ColorIdentifier& colorId)
{
    std::vector<unsigned char> scriptHash(20, static_cast<unsigned char>(0x22));
    return CScript() << colorId.toVector() << OP_COLOR << OP_HASH160 << scriptHash << OP_EQUAL;
}

// OP_COLOR in a script that is neither CP2PKH nor CP2SH.
CScript NonstandardColoredScript(const ColorIdentifier& colorId)
{
    return CScript() << colorId.toVector() << OP_COLOR << OP_TRUE;
}

// A colored script for colorId in one of the three shapes.
CScript ColoredScript(const ColorIdentifier& colorId)
{
    switch (InsecureRandRange(4)) {
        case 0: return Cp2shScript(colorId);
        case 1: return NonstandardColoredScript(colorId);
        default: return Cp2pkhScript(colorId);
    }
}

// A coin of value -1 counts as spent, so only outputs may draw it.
CAmount RandomValue(bool allowNegative)
{
    static const std::vector<CAmount> values{CAmount{0}, CAmount{1}, CAmount{2}, CAmount{1000}, CAmount{50000}, MAX_MONEY, CAmount{-1}};
    return values.at(InsecureRandRange(values.size() - (allowNegative ? 0 : 1)));
}

ColorIdentifier PickColorId(const std::vector<ColorIdentifier>& colorIds)
{
    return colorIds.at(InsecureRandRange(colorIds.size()));
}

struct Outcome {
    bool ok;
    std::string reason;
    uint32_t code;
    int32_t dos;
    std::set<ColorIdentifier> issuances;
};

Outcome Capture(bool ok, const CValidationState& state, std::set<ColorIdentifier> issuances = {})
{
    int dos{0};
    state.IsInvalid(dos);
    return Outcome{ok, state.GetRejectReason(), state.GetRejectCode(), static_cast<int32_t>(dos), std::move(issuances)};
}

void CheckSame(const Outcome& legacy, const Outcome& checker)
{
    BOOST_CHECK_EQUAL(legacy.ok, checker.ok);
    BOOST_CHECK_EQUAL(legacy.reason, checker.reason);
    BOOST_CHECK_EQUAL(legacy.code, checker.code);
    BOOST_CHECK_EQUAL(legacy.dos, checker.dos);
    BOOST_CHECK(legacy.issuances == checker.issuances);
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(transactioncolorchecker_tests, BasicTestingSetup)

// Random transactions over TPC, colored, non-standard, spent and missing
// coins must get the same result, reject reason, code, DoS score and new
// issuances from TransactionColorChecker as from the old functions.
BOOST_AUTO_TEST_CASE(checker_matches_legacy_functions)
{
    const uint32_t iterations{4000};
    uint32_t outputChecksPassed{0};
    uint32_t balanceChecksPassed{0};

    for (uint32_t iter = 0; iter < iterations; ++iter) {
        CCoinsView base;
        CCoinsViewCache view(&base);
        CMutableTransaction mtx;
        mtx.nFeatures = 1;

        // Colorids the outputs may carry: issuable from an input, held by an
        // input, or unrelated.
        std::vector<ColorIdentifier> candidates;
        candidates.emplace_back(COutPoint(InsecureRand256(), 0), TokenTypes::REISSUABLE);

        const uint32_t inputCount = static_cast<uint32_t>(InsecureRandRange(4));
        for (uint32_t i = 0; i < inputCount; ++i) {
            const COutPoint prevout(InsecureRand256(), static_cast<uint32_t>(InsecureRandRange(3)));
            mtx.vin.emplace_back(prevout);

            const CScript tpc = TpcScript(i);
            candidates.emplace_back(tpc);
            candidates.emplace_back(prevout, TokenTypes::NON_REISSUABLE);
            candidates.emplace_back(prevout, TokenTypes::NFT);

            const uint32_t kind = static_cast<uint32_t>(InsecureRandRange(10));
            if (kind == 9)
                continue; // missing coin
            Coin coin;
            coin.out.nValue = RandomValue(false);
            coin.nHeight = 1;
            coin.out.scriptPubKey = kind < 5 ? tpc : ColoredScript(PickColorId(candidates));
            view.AddCoin(prevout, std::move(coin), false);
            if (kind == 8)
                view.SpendCoin(prevout);
        }

        const uint32_t outputCount = static_cast<uint32_t>(InsecureRandRange(4));
        for (uint32_t i = 0; i < outputCount; ++i) {
            // Repeat the previous output now and then, so one colorid lands on
            // several outputs, as NFT duplicates and supply splits do.
            if (i > 0 && InsecureRandRange(4) == 0) {
                const CTxOut previous = mtx.vout.back();
                mtx.vout.push_back(previous);
                continue;
            }
            if (InsecureRandRange(3) == 0) {
                mtx.vout.emplace_back(RandomValue(true), TpcScript(i));
                continue;
            }
            // An NFT output often carries its only valid value, 1.
            const ColorIdentifier colorId = PickColorId(candidates);
            const CAmount value = colorId.type == TokenTypes::NFT && InsecureRandBool() ? CAmount{1} : RandomValue(true);
            mtx.vout.emplace_back(value, ColoredScript(colorId));
        }
        const CTransaction tx(mtx);

        {
            LOCK(cs_main);
            g_colorid_state.reset(new CIssuedColorIds());
            std::set<ColorIdentifier> issued;
            for (const ColorIdentifier& colorId : candidates) {
                if (InsecureRandRange(4) == 0)
                    issued.insert(colorId);
            }
            g_colorid_state->Insert(issued);
        }

        const int32_t height = InsecureRandBool() ? int32_t{0} : std::numeric_limits<int32_t>::max();
        const CAmount minFee = InsecureRandBool() ? CAmount{0} : CAmount{1000};

        LOCK(cs_main);
        PrecomputedColorData data(tx, view, height);
        data.SetIssued(*g_colorid_state);
        const TransactionColorChecker checker(data);

        CValidationState legacyOutputState, checkerOutputState;
        const bool legacyOutputOk = LegacyCheckColorIdentifierValidity(tx, legacyOutputState, view, height);
        const bool checkerOutputOk = checker.CheckOutputColorIds(checkerOutputState);
        CheckSame(Capture(legacyOutputOk, legacyOutputState), Capture(checkerOutputOk, checkerOutputState));
        if (checkerOutputOk)
            ++outputChecksPassed;

        CValidationState legacyBalanceState, checkerBalanceState;
        std::set<ColorIdentifier> legacyIssuances, checkerIssuances;
        const bool legacyBalanceOk = LegacyVerifyTokenBalances(tx, legacyBalanceState, view, minFee, &legacyIssuances, height);
        const bool checkerBalanceOk = checker.CheckTokenBalances(checkerBalanceState, minFee);
        if (checkerBalanceOk) {
            checker.GetNewIssuances(checkerIssuances);
            ++balanceChecksPassed;
        }
        CheckSame(Capture(legacyBalanceOk, legacyBalanceState, legacyIssuances),
                  Capture(checkerBalanceOk, checkerBalanceState, checkerIssuances));
    }

    // Both checks must have passed and failed often enough to compare anything.
    BOOST_CHECK_GT(outputChecksPassed, iterations / 20);
    BOOST_CHECK_LT(outputChecksPassed, iterations);
    BOOST_CHECK_GT(balanceChecksPassed, iterations / 20);
    BOOST_CHECK_LT(balanceChecksPassed, iterations);
}

// OP_COLOR's colorid must equal the spent coin's precomputed colorid; a coin
// without one, such as a non-standard OP_COLOR coin, is not compared.
BOOST_AUTO_TEST_CASE(colored_signature_checker_compares_spent_colorid)
{
    const ColorIdentifier spent(COutPoint(InsecureRand256(), 0), TokenTypes::NON_REISSUABLE);
    const ColorIdentifier other(COutPoint(InsecureRand256(), 1), TokenTypes::NON_REISSUABLE);
    const CTransaction tx{CMutableTransaction()};

    auto eval = [&](const ColorIdentifier& scriptColorId, const ColorIdentifier& spentColorId, ScriptError& err) {
        std::vector<std::vector<unsigned char>> stack;
        ColorIdentifier read;
        const CScript script = CScript() << scriptColorId.toVector() << OP_COLOR << OP_TRUE;
        return EvalScript(stack, script, SCRIPT_VERIFY_NONE, ColoredSignatureChecker(&tx, 0, CAmount{0}, false, spentColorId), &read, &err);
    };

    ScriptError err;
    BOOST_CHECK(eval(spent, spent, err));
    BOOST_CHECK(!eval(other, spent, err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OP_COLORID_MISMATCH);
    BOOST_CHECK(eval(other, ColorIdentifier(), err));
}

BOOST_AUTO_TEST_SUITE_END()
