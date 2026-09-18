// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// tapyrus-verify: evaluates whether a `spending` transaction correctly
// spends one output of a `to_spend` transaction, the to_spend/spending
// pair shape Bitcoin's BIP-341/342 test vectors use.
//
// A PASS is a necessary-but-not-sufficient signal for one input's script
// correctness -- not a node-acceptance guarantee. It checks only the one
// input given, not the whole transaction's balance or other inputs; it
// has no live chain state, so it can't confirm to_spend is actually
// unspent or that a height-gated soft fork like CP2SH_COLORED is active
// on any real network; and it never runs mempool/relay policy checks
// (IsStandardTx) at all.
//
// Links tapyrus_consensus plus tapyrus_common only (no tapyrus_server,
// no leveldb, no networking, no block-level validation). tapyrus_common
// is needed only so VerifyScript's CP2SH_COLORED check can read the
// global FederationParams() singleton -- see main().
//
// spending.vin[input_index].prevout, not input_index itself, says which
// to_spend output is being spent (.hashMalFix identifies the
// transaction, .n the output index). This tool verifies prevout
// actually matches to_spend before running VerifyScript.
//
// By default, checks a given pair against every one of Tapyrus's 64
// runtime-selectable SCRIPT_VERIFY_* flag combinations (script/interpreter.h)
// and reports where the result changes -- this includes flags=0
// (MANDATORY_SCRIPT_VERIFY_FLAGS) and the exact STANDARD_SCRIPT_VERIFY_FLAGS
// combination, both being unions of those same flags. --flags=<spec> runs
// one set instead of the full matrix.
//
// --fuzz <script-mnemonic-file> (only when built with BUILD_FUZZ_TEST --
// the oracle behind contrib/fuzz/fuzz4all/TAPYRUSSCRIPT.py) assembles the
// file into a scriptPubKey, builds its own to_spend/spending pair, and
// additionally sweeps 16 nLockTime/nSequence combinations -- each one
// checked the same way (full flag matrix, or --flags=<spec> if given).
//
// Exit codes:
//   0 = ran cleanly, including a script that failed verification
//       normally (bad signature, stack underflow, etc).
//   1 = usage error, invalid input, or a to_spend/spending pair that
//       doesn't match.
//   2 = (--fuzz only) the script-mnemonic text was rejected by
//       ParseScript as not valid Script -- an LLM_WEAKNESS-shaped
//       result, not a bug in Tapyrus.
//   killed by a signal (SIGSEGV/SIGABRT from ASan/UBSan, etc) = a crash.

#include <tapyrus-config.h>

#include <amount.h>
#include <chainparams.h>
#include <coloridentifier.h>
#include <federationparams.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <streams.h>
#include <tapyrusmodes.h>
#include <utilstrencodings.h>
#include <version.h>

#ifdef BUILD_FUZZ_TEST
#include <core_io.h>
#endif

#include <cstdio>
#ifdef BUILD_FUZZ_TEST
#include <fstream>
#endif
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_ERROR = 1;
#ifdef BUILD_FUZZ_TEST
constexpr int EXIT_REJECTED_BY_ASSEMBLER = 2;
#endif

struct NamedFlag {
    const char* name;
    unsigned int value;
};

// Tapyrus's runtime-selectable SCRIPT_VERIFY_* flags (script/interpreter.h).
// Everything else (P2SH, STRICTENC, DERSIG, LOW_S, NULLDUMMY, MINIMALDATA,
// CLTV, CSV) is unconditionally enforced (MANDATORY_SCRIPT_VERIFY_FLAGS == 0).
constexpr NamedFlag kNamedFlags[] = {
    {"sigpushonly", SCRIPT_VERIFY_SIGPUSHONLY},
    {"discourage-upgradable-nops", SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS},
    {"cleanstack", SCRIPT_VERIFY_CLEANSTACK},
    {"nullfail", SCRIPT_VERIFY_NULLFAIL},
    {"const-scriptcode", SCRIPT_VERIFY_CONST_SCRIPTCODE},
    {"cp2sh-colored", SCRIPT_VERIFY_CP2SH_COLORED},
};
constexpr size_t kNumNamedFlags = sizeof(kNamedFlags) / sizeof(kNamedFlags[0]);
constexpr unsigned int kAllNamedFlagsMask = (1u << kNumNamedFlags) - 1u;

bool DecodeTx(const std::string& hex, CMutableTransaction& tx)
{
    std::vector<unsigned char> data;
    try {
        data = ParseHex(hex);
    } catch (const std::exception&) {
        return false;
    }
    if (data.empty()) return false;
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> tx;
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

template <typename Tx>
std::string EncodeHexTx(const Tx& tx)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << tx;
    return HexStr(ss);
}

// Checks output values are individually and cumulatively in range.
bool CheckMoneyRange(const CTransaction& tx, const char* label)
{
    if (tx.vout.empty()) {
        std::fprintf(stderr, "error: %s has no outputs\n", label);
        return false;
    }
    CAmount total = 0;
    for (const auto& out : tx.vout) {
        if (out.nValue < 0 || out.nValue > MAX_MONEY) {
            std::fprintf(stderr, "error: %s has an output value out of range\n", label);
            return false;
        }
        total += out.nValue;
        if (!MoneyRange(total)) {
            std::fprintf(stderr, "error: %s's total output value is out of range\n", label);
            return false;
        }
    }
    return true;
}

// Rejects a transaction that spends the same outpoint twice.
bool CheckNoDuplicateInputs(const CMutableTransaction& tx, const char* label)
{
    std::set<COutPoint> seen;
    for (const auto& in : tx.vin) {
        if (!seen.insert(in.prevout).second) {
            std::fprintf(stderr, "error: %s has a duplicate input\n", label);
            return false;
        }
    }
    return true;
}

// Same check core_read.cpp's CheckTxScriptsSanity applies when decoding a
// transaction from hex, applied here to every scriptSig/scriptPubKey.
template <typename Tx>
bool CheckAllScriptsSanity(const Tx& tx, const char* label)
{
    for (const auto& in : tx.vin) {
        if (!in.scriptSig.HasValidOps() || in.scriptSig.size() > MAX_SCRIPT_SIZE) {
            std::fprintf(stderr, "error: %s has an invalid or oversized scriptSig\n", label);
            return false;
        }
    }
    for (const auto& out : tx.vout) {
        if (!out.scriptPubKey.HasValidOps() || out.scriptPubKey.size() > MAX_SCRIPT_SIZE) {
            std::fprintf(stderr, "error: %s has an invalid or oversized scriptPubKey\n", label);
            return false;
        }
    }
    return true;
}

// A loaded, validated (toSpend, spending) pair, with the input and
// output indices to verify already resolved from spending's own prevout.
struct VerifyContext {
    CTransaction toSpend;
    CMutableTransaction spending;
    unsigned int nIn;
    unsigned int outN;
};

// Deserializes both transactions, checks they're well-formed, and
// resolves which output of toSpend spending's input at nIn actually
// claims to spend from spending.vin[nIn].prevout -- not from nIn
// itself, which is an input index into `spending`, not an output index
// into `toSpend`. Confirms prevout.hashMalFix identifies toSpend and
// prevout.n is one of its outputs, rejecting a mismatched pair instead
// of comparing against an unrelated output.
//
// Returns by value (nullopt on failure): CTransaction's members are
// const, so it has no copy-assignment operator, only copy/move
// construction, ruling out an out-param filled after default-construction.
std::optional<VerifyContext> LoadPair(const std::string& toSpendHex, const std::string& spendingHex,
                                       unsigned int nIn)
{
    CMutableTransaction toSpendMut, spendingMut;
    if (!DecodeTx(toSpendHex, toSpendMut) || !DecodeTx(spendingHex, spendingMut)) {
        std::fprintf(stderr, "error: could not deserialize one or both transactions\n");
        return std::nullopt;
    }
    CTransaction toSpend{toSpendMut};

    if (!CheckMoneyRange(toSpend, "to_spend")) return std::nullopt;
    if (!CheckAllScriptsSanity(toSpend, "to_spend")) return std::nullopt;
    if (spendingMut.vin.empty()) {
        std::fprintf(stderr, "error: spending has no inputs\n");
        return std::nullopt;
    }
    if (!CheckNoDuplicateInputs(spendingMut, "spending")) return std::nullopt;
    const CTransaction spendingConst{spendingMut};
    if (!CheckMoneyRange(spendingConst, "spending")) return std::nullopt;
    if (!CheckAllScriptsSanity(spendingConst, "spending")) return std::nullopt;

    if (nIn >= spendingMut.vin.size()) {
        std::fprintf(stderr, "error: input index %u out of range (spending tx has %zu input(s))\n",
                     nIn, spendingMut.vin.size());
        return std::nullopt;
    }
    const COutPoint& prevout = spendingMut.vin[nIn].prevout;
    if (prevout.hashMalFix != toSpend.GetHashMalFix()) {
        std::fprintf(stderr,
                      "error: spending.vin[%u].prevout does not reference to_spend "
                      "(expected hashMalFix=%s, got %s) -- these are not a matching pair\n",
                      nIn, toSpend.GetHashMalFix().ToString().c_str(),
                      prevout.hashMalFix.ToString().c_str());
        return std::nullopt;
    }
    if (prevout.n >= toSpend.vout.size()) {
        std::fprintf(stderr,
                      "error: spending.vin[%u].prevout.n=%u out of range (to_spend has %zu output(s))\n",
                      nIn, prevout.n, toSpend.vout.size());
        return std::nullopt;
    }
    unsigned int outN = prevout.n;
    return VerifyContext{std::move(toSpend), std::move(spendingMut), nIn, outN};
}

struct FlagResult {
    bool ok;
    ScriptError err;
};

// A memory-safety crash (ASan/UBSan abort, SIGSEGV, etc) terminates the
// process via signal instead of returning here -- the return value is
// only diagnostic, not what this tool treats as success or failure.
FlagResult RunOneFlag(const VerifyContext& ctx, unsigned int flags)
{
    ColorIdentifier colorId;
    ScriptError err;
    bool ok = VerifyScript(
        ctx.spending.vin[ctx.nIn].scriptSig,
        ctx.toSpend.vout[ctx.outN].scriptPubKey,
        flags,
        MutableTransactionSignatureChecker(&ctx.spending, ctx.nIn, ctx.toSpend.vout[ctx.outN].nValue),
        colorId, &err);
    return {ok, err};
}

// Maps a compact 0..63 mask (bit i = kNamedFlags[i]) to the real
// SCRIPT_VERIFY_* bitmask -- the named flags sit at non-sequential bit
// positions (nullfail is bit 14, for example), so a mask's bit index
// can't be used as the real flags value directly.
unsigned int MaskToFlags(unsigned int mask)
{
    unsigned int flags = 0;
    for (size_t i = 0; i < kNumNamedFlags; ++i) {
        if (mask & (1u << i)) flags |= kNamedFlags[i].value;
    }
    return flags;
}

std::string FlagsToNames(unsigned int flags)
{
    if (flags == SCRIPT_VERIFY_NONE) return "none";
    std::string result;
    for (const auto& nf : kNamedFlags) {
        if (flags & nf.value) {
            if (!result.empty()) result += '|';
            result += nf.name;
        }
    }
    return result;
}

// Parses a hex digit string (no "0x" prefix) into an unsigned int without
// strtoul/isxdigit, both locale-dependent -- explicit char-range checks
// only.
bool ParseHexUInt32(const std::string& hex, unsigned int& out)
{
    if (hex.empty() || hex.size() > 8) return false;
    unsigned int value = 0;
    for (char c : hex) {
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
        else return false;
    }
    out = value;
    return true;
}

bool ParseFlags(const std::string& spec, unsigned int& outFlags)
{
    if (spec == "standard") { outFlags = STANDARD_SCRIPT_VERIFY_FLAGS; return true; }
    if (spec == "none") { outFlags = SCRIPT_VERIFY_NONE; return true; }
    if (spec.size() > 2 && spec[0] == '0' && (spec[1] == 'x' || spec[1] == 'X')) {
        if (!ParseHexUInt32(spec.substr(2), outFlags)) {
            std::fprintf(stderr, "error: invalid hex flags value: %s\n", spec.c_str());
            return false;
        }
        return true;
    }
    unsigned int result = 0;
    std::stringstream ss(spec);
    std::string name;
    while (std::getline(ss, name, ',')) {
        bool matched = false;
        for (const auto& nf : kNamedFlags) {
            if (name == nf.name) { result |= nf.value; matched = true; break; }
        }
        if (!matched) {
            std::fprintf(stderr, "error: unknown flag name: %s\n", name.c_str());
            return false;
        }
    }
    outFlags = result;
    return true;
}

// ParseUInt32 (utilstrencodings.h) is locale-independent (std::from_chars)
// and already rejects a leading '-' -- distinguish that case in the error
// message rather than lumping it in with other malformed input.
bool ParseIndex(const std::string& spec, unsigned int& outIndex)
{
    uint32_t val;
    if (!ParseUInt32(spec, &val)) {
        if (!spec.empty() && spec[0] == '-') {
            std::fprintf(stderr, "error: input index must be a non-negative integer, got: %s\n", spec.c_str());
        } else {
            std::fprintf(stderr, "error: invalid input index: %s\n", spec.c_str());
        }
        return false;
    }
    outIndex = val;
    return true;
}

void PrintPairHex(const VerifyContext& ctx)
{
    std::fprintf(stderr, "to_spend=%s spending=%s\n",
                 EncodeHexTx(ctx.toSpend).c_str(), EncodeHexTx(ctx.spending).c_str());
}

int RunSingleFlags(const VerifyContext& ctx, unsigned int flags)
{
    PrintPairHex(ctx);
    FlagResult r = RunOneFlag(ctx, flags);
    std::fprintf(stderr, "flags=0x%x [%s] %s (%s)\n", flags, FlagsToNames(flags).c_str(),
                 r.ok ? "PASS" : "FAIL", ScriptErrorString(r.err));
    return EXIT_OK;
}

// Runs the pair against all 64 combinations of the named flags --
// including flags=0 (MANDATORY_SCRIPT_VERIFY_FLAGS, which is 0 in
// Tapyrus) and the exact STANDARD_SCRIPT_VERIFY_FLAGS combination, both
// being unions of these same named flags -- and reports where the
// result changes. Also checks the soft-fork monotonicity invariant
// script/interpreter.h documents (adding flags can only shrink the
// accepted-script set): a superset of flags passing while a subset
// fails would mean one of these flags isn't behaving as a soft fork.
int RunFlagsMatrix(const VerifyContext& ctx)
{
    PrintPairHex(ctx);
    bool results[kAllNamedFlagsMask + 1];
    for (unsigned int mask = 0; mask <= kAllNamedFlagsMask; ++mask) {
        unsigned int flags = MaskToFlags(mask);
        FlagResult r = RunOneFlag(ctx, flags);
        results[mask] = r.ok;
        std::fprintf(stderr, "0x%02x [%-70s] %s (%s)\n", flags, FlagsToNames(flags).c_str(),
                     r.ok ? "PASS" : "FAIL", ScriptErrorString(r.err));
    }

    int violations = 0;
    for (unsigned int a = 0; a <= kAllNamedFlagsMask; ++a) {
        for (unsigned int b = 0; b <= kAllNamedFlagsMask; ++b) {
            if ((a & b) != a || a == b) continue; // only a proper subset of b
            if (!results[a] && results[b]) {
                std::fprintf(stderr,
                              "MONOTONICITY VIOLATION: flags=0x%02x [%s] FAILS but its "
                              "superset flags=0x%02x [%s] PASSES\n",
                              MaskToFlags(a), FlagsToNames(MaskToFlags(a)).c_str(),
                              MaskToFlags(b), FlagsToNames(MaskToFlags(b)).c_str());
                ++violations;
            }
        }
    }
    std::fprintf(stderr, "%u combination(s) tested, %d monotonicity violation(s) found.\n",
                 kAllNamedFlagsMask + 1, violations);
    return EXIT_OK;
}

// Runs either the full flags matrix or one explicit flag set, depending
// on whether the caller passed --flags.
int RunOne(const VerifyContext& ctx, bool flagsGiven, unsigned int flags)
{
    return flagsGiven ? RunSingleFlags(ctx, flags) : RunFlagsMatrix(ctx);
}

#ifdef BUILD_FUZZ_TEST

// nLockTime: disabled, smallest height-based, the historical
// height-vs-time-locktime boundary, and the max uint32 value.
// nSequence: SEQUENCE_FINAL (locktime and relative-locktime both
// disabled), 0 (both enabled), the relative-locktime-disable bit set
// alone, and one high-but-not-final value. Not exhaustive, just varied.
constexpr uint32_t kLockTimes[] = {0, 1, 500000000, 0xFFFFFFFFu};
constexpr uint32_t kSequences[] = {CTxIn::SEQUENCE_FINAL, 0, 0x80000000u, 0xFFFFFFFEu};
constexpr size_t kTotalVariations = (sizeof(kLockTimes) / sizeof(kLockTimes[0])) *
                                     (sizeof(kSequences) / sizeof(kSequences[0]));

CMutableTransaction BuildToSpend(const CScript& scriptPubKey)
{
    CMutableTransaction tx;
    tx.nFeatures = 1;
    tx.nLockTime = 0;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL;
    tx.vout[0].scriptPubKey = scriptPubKey;
    tx.vout[0].nValue = 0;
    return tx;
}

CMutableTransaction BuildSpending(const CTransaction& toSpend, uint32_t nLockTime, uint32_t nSequence)
{
    CMutableTransaction tx;
    tx.nFeatures = 1;
    tx.nLockTime = nLockTime;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vin[0].prevout.hashMalFix = toSpend.GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript();
    tx.vin[0].nSequence = nSequence;
    tx.vout[0].scriptPubKey = CScript();
    tx.vout[0].nValue = toSpend.vout[0].nValue;
    return tx;
}

// Assembles scriptFile into a scriptPubKey, builds its own to_spend/
// spending pair, and runs each of kTotalVariations nLockTime/nSequence
// combinations through RunOne.
int RunFuzzMode(const std::string& scriptFile, bool flagsGiven, unsigned int flags)
{
    std::ifstream in(scriptFile);
    if (!in) {
        std::fprintf(stderr, "error: cannot open %s\n", scriptFile.c_str());
        return EXIT_ERROR;
    }
    std::ostringstream buf;
    buf << in.rdbuf();

    CScript scriptPubKey;
    try {
        scriptPubKey = ParseScript(buf.str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "rejected by assembler: %s\n", e.what());
        return EXIT_REJECTED_BY_ASSEMBLER;
    }

    const CTransaction toSpend{BuildToSpend(scriptPubKey)};

    size_t variation = 0;
    for (uint32_t lockTime : kLockTimes) {
        for (uint32_t sequence : kSequences) {
            ++variation;
            std::fprintf(stderr, "-- variation %zu/%zu: nLockTime=%u nSequence=%u --\n",
                         variation, kTotalVariations, lockTime, sequence);
            VerifyContext ctx{toSpend, BuildSpending(toSpend, lockTime, sequence), 0, 0};
            RunOne(ctx, flagsGiven, flags);
        }
    }
    return EXIT_OK;
}

#endif // BUILD_FUZZ_TEST

void PrintUsage(const char* argv0)
{
    std::fprintf(stderr,
        "usage: %s <to_spend_hex> <spending_hex> [input_index] [--flags=<spec>]\n"
        "\n"
        "  Runs every one of the %zu flag combinations below by default and\n"
        "  reports where the result changes; --flags=<spec> runs one instead.\n"
        "\n"
        "  input_index    index into spending's own inputs (default 0); the output\n"
        "                 verified is whichever one spending.vin[input_index].prevout\n"
        "                 actually points at in to_spend, not input_index itself.\n"
        "  --flags=<spec> \"standard\" (STANDARD_SCRIPT_VERIFY_FLAGS), \"none\"\n"
        "                 (MANDATORY_SCRIPT_VERIFY_FLAGS), a 0x-prefixed hex bitmask,\n"
        "                 or a comma-separated list of:\n",
        argv0, size_t{1} << kNumNamedFlags);
    for (const auto& nf : kNamedFlags) {
        std::fprintf(stderr, "                   %s\n", nf.name);
    }
#ifdef BUILD_FUZZ_TEST
    std::fprintf(stderr,
        "\n"
        "       %s --fuzz <script-mnemonic-file> [--flags=<spec>]\n"
        "  --fuzz <file>  assembles a Script-mnemonic file into scriptPubKey, builds\n"
        "                 its own to_spend/spending pair, and additionally sweeps %zu\n"
        "                 nLockTime/nSequence combinations\n",
        argv0, kTotalVariations);
#endif
}

} // namespace

int main(int argc, char* argv[])
{
    // VerifyScript's CP2SH_COLORED check needs the global FederationParams()
    // singleton selected before any call, same as tapyrus-cli/tapyrus-genesis.
    SelectParams(TAPYRUS_OP_MODE::PROD);
    SelectFederationParams(TAPYRUS_OP_MODE::PROD, false);

    std::vector<std::string> positional;
    bool flagsGiven = false;
    std::string flagsSpec;
#ifdef BUILD_FUZZ_TEST
    bool fuzzMode = false;
#endif
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--flags=", 0) == 0) {
            flagsSpec = arg.substr(8);
            flagsGiven = true;
#ifdef BUILD_FUZZ_TEST
        } else if (arg == "--fuzz") {
            fuzzMode = true;
#endif
        } else {
            positional.push_back(arg);
        }
    }

    unsigned int flags = 0;
    if (flagsGiven && !ParseFlags(flagsSpec, flags)) {
        return EXIT_ERROR;
    }

#ifdef BUILD_FUZZ_TEST
    if (fuzzMode) {
        if (positional.size() != 1) {
            PrintUsage(argv[0]);
            return EXIT_ERROR;
        }
        return RunFuzzMode(positional[0], flagsGiven, flags);
    }
#endif

    if (positional.size() != 2 && positional.size() != 3) {
        PrintUsage(argv[0]);
        return EXIT_ERROR;
    }
    unsigned int nIn = 0;
    if (positional.size() == 3 && !ParseIndex(positional[2], nIn)) {
        return EXIT_ERROR;
    }

    std::optional<VerifyContext> ctx = LoadPair(positional[0], positional[1], nIn);
    if (!ctx) {
        return EXIT_ERROR;
    }
    return RunOne(*ctx, flagsGiven, flags);
}
