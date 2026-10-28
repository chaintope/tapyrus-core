// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <policy/policysettings.h>

#include <primitives/block.h>
#include <script/script.h>
#include <tapyrusmodes.h>
#include <tinyformat.h>
#include <ui_interface.h>
#include <util.h>
#include <utilmoneystr.h>

#include <boost/algorithm/string.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

const PolicySettings* g_policy_settings = nullptr;

CFeeRate PolicySettings::ParseFeeRate(const ArgsManager& args, const std::string& name, CAmount defaultFee)
{
    if (!args.IsArgSet("-" + name)) return CFeeRate(defaultFee);
    CAmount n = 0;
    if (!ParseMoney(args.GetArg("-" + name, ""), n))
        throw std::runtime_error(AmountErrMsg(name.c_str(), args.GetArg("-" + name, "")));
    return CFeeRate(n);
}

CFeeRate PolicySettings::ParseMinRelayTxFee(const ArgsManager& args, const CFeeRate& incrementalRelayFee)
{
    if (args.IsArgSet("-minrelaytxfee")) {
        // High fee check is done when the wallet is created
        return ParseFeeRate(args, "minrelaytxfee", DEFAULT_MIN_RELAY_TX_FEE);
    }
    const CFeeRate minRelayTxFee(DEFAULT_MIN_RELAY_TX_FEE);
    if (incrementalRelayFee > minRelayTxFee) {
        // Allow only setting incrementalRelayFee to control both
        LogPrintf("Increasing minrelaytxfee to %s to match incrementalrelayfee\n", incrementalRelayFee.ToString());
        return incrementalRelayFee;
    }
    return minRelayTxFee;
}

uint32_t PolicySettings::ParseBytesPerSigOp(const ArgsManager& args)
{
    const int64_t bytesPerSigOp = args.GetArg("-bytespersigop", DEFAULT_BYTES_PER_SIGOP);
    if (bytesPerSigOp < 1 || bytesPerSigOp > (int64_t)std::numeric_limits<unsigned int>::max()) {
        throw std::runtime_error(strprintf("-bytespersigop value %d is out of range [1, %u]", bytesPerSigOp, std::numeric_limits<unsigned int>::max()));
    }
    return (uint32_t)bytesPerSigOp;
}

uint32_t PolicySettings::ParseMaxDatacarrierBytes(const ArgsManager& args)
{
    const int64_t nArg = args.GetArg("-datacarriersize", MAX_OP_RETURN_RELAY);
    if (nArg < 0)
        throw std::runtime_error(_("-datacarriersize cannot be configured with a negative value."));
    if (nArg > std::numeric_limits<unsigned int>::max())
        throw std::runtime_error(_("-datacarriersize must be less than 4294967296."));
    return (uint32_t)nArg;
}

bool PolicySettings::ParseAcceptNonStdTxn(const ArgsManager& args, TAPYRUS_OP_MODE mode)
{
#ifdef DEBUG
    const bool acceptNonStdTxn = args.GetBoolArg("-acceptnonstdtxn", false);
    if (mode == TAPYRUS_OP_MODE::PROD && acceptNonStdTxn)
        throw std::runtime_error(strprintf("acceptnonstdtxn is not supported for %s chain", TAPYRUS_MODES::GetChainName(TAPYRUS_OP_MODE::PROD)));
    return acceptNonStdTxn;
#else
    return false;
#endif
}

bool PolicySettings::ParseEnableReplacement(const ArgsManager& args)
{
    bool enableReplacement = args.GetBoolArg("-mempoolreplacement", DEFAULT_ENABLE_REPLACEMENT);
    if (!enableReplacement && args.IsArgSet("-mempoolreplacement")) {
        // Minimal effort at forwards compatibility
        std::string strReplacementModeList = args.GetArg("-mempoolreplacement", "");  // default is impossible
        std::vector<std::string> vstrReplacementModes;
        boost::split(vstrReplacementModes, strReplacementModeList, boost::is_any_of(","));
        enableReplacement = (std::find(vstrReplacementModes.begin(), vstrReplacementModes.end(), "fee") != vstrReplacementModes.end());
    }
    return enableReplacement;
}

int64_t PolicySettings::ParseMaxMempool(const ArgsManager& args)
{
    const int64_t nMempoolSizeMax = args.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    const int64_t nMempoolSizeMin = args.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000 * 40;
    if (nMempoolSizeMax < 0 || nMempoolSizeMax < nMempoolSizeMin)
        throw std::runtime_error(strprintf(_("-maxmempool must be at least %d MB"), std::ceil(nMempoolSizeMin / 1000000.0)));
    return nMempoolSizeMax;
}

int64_t PolicySettings::ParseInRange(const ArgsManager& args, const std::string& name, int64_t defaultValue, int64_t min, int64_t max)
{
    const int64_t value = args.GetArg("-" + name, defaultValue);
    if (value < min || value > max) {
        throw std::runtime_error(strprintf("-%s value %d is out of range [%d, %d]", name, value, min, max));
    }
    return value;
}

uint32_t PolicySettings::ParseMinStandardTxSize(const ArgsManager& args, uint32_t maxStandardTxSize)
{
    return static_cast<uint32_t>(ParseInRange(args, "minstandardtxsize", DEFAULT_MIN_STANDARD_TX_SIZE, 0, maxStandardTxSize));
}

PolicySettings::PolicySettings(const ArgsManager& args, TAPYRUS_OP_MODE mode)
    : incrementalRelayFee(ParseFeeRate(args, "incrementalrelayfee", DEFAULT_INCREMENTAL_RELAY_FEE)),
      minRelayTxFee(ParseMinRelayTxFee(args, incrementalRelayFee)),
      // Feerate used to define dust. Shouldn't be changed lightly as old
      // implementations may inadvertently create non-standard transactions
      dustRelayFee(ParseFeeRate(args, "dustrelayfee", DUST_RELAY_TX_FEE)),
      blockMinTxFee(ParseFeeRate(args, "blockmintxfee", DEFAULT_BLOCK_MIN_TX_FEE)),
      bytesPerSigOp(ParseBytesPerSigOp(args)),
      acceptDatacarrier(args.GetBoolArg("-datacarrier", DEFAULT_ACCEPT_DATACARRIER)),
      maxDatacarrierBytes(ParseMaxDatacarrierBytes(args)),
      acceptMultipleDatacarrier(args.GetBoolArg("-datacarriermultiple", DEFAULT_ACCEPT_MULTIPLE_DATACARRIER)),
      acceptNonStdTxn(ParseAcceptNonStdTxn(args, mode)),
      enableReplacement(ParseEnableReplacement(args)),
      maxMempool(ParseMaxMempool(args)),
      mempoolExpiry(args.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60),
      persistMempool(args.GetBoolArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)),
      ancestorLimit(args.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT)),
      ancestorSizeLimit(args.GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT) * 1000),
      descendantLimit(args.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT)),
      descendantSizeLimit(args.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000),
      maxOrphanTx((uint32_t)std::max((int64_t)0, args.GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS))),
      blockReconstructionExtraTxn(args.GetArg("-blockreconstructionextratxn", DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN)),
      feeFilter(args.GetBoolArg("-feefilter", DEFAULT_FEEFILTER)),
      whitelistRelay(args.GetBoolArg("-whitelistrelay", DEFAULT_WHITELISTRELAY)),
      whitelistForceRelay(args.GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY)),
      maxStandardTxSize(static_cast<uint32_t>(ParseInRange(args, "maxstandardtxsize", DEFAULT_MAX_STANDARD_TX_SIZE, 1, ConsensusConstants::MAX_BLOCK_SIZE))),
      minStandardTxSize(ParseMinStandardTxSize(args, maxStandardTxSize)),
      maxScriptSigSize(static_cast<uint32_t>(ParseInRange(args, "maxscriptsigsize", DEFAULT_MAX_SCRIPTSIG_SIZE, 0, std::numeric_limits<uint32_t>::max()))),
      maxBareMultisigKeys(static_cast<uint32_t>(ParseInRange(args, "maxbaremultisigkeys", DEFAULT_MAX_BAREMULTISIG_KEYS, 1, ConsensusConstants::MAX_PUBKEYS_PER_MULTISIG))),
      maxP2shSigOps(static_cast<uint32_t>(ParseInRange(args, "maxp2shsigops", DEFAULT_MAX_P2SH_SIGOPS, 0, std::numeric_limits<uint32_t>::max()))),
      maxReplacementCandidates(static_cast<uint32_t>(ParseInRange(args, "maxreplacementcandidates", DEFAULT_MAX_REPLACEMENT_CANDIDATES, 0, std::numeric_limits<uint32_t>::max()))),
      orphanExpiry(ParseInRange(args, "orphanexpiry", DEFAULT_ORPHAN_TX_EXPIRE_TIME, 1, std::numeric_limits<uint32_t>::max())),
      maxPackageCount(static_cast<uint32_t>(ParseInRange(args, "maxpackagecount", DEFAULT_MAX_PACKAGE_COUNT, 1, std::numeric_limits<uint32_t>::max()))),
      blockMaxSize((uint32_t)args.GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE)),
      // -dev only: lets tests mine blocks with other features to test forking scenarios
      blockFeatures(mode == TAPYRUS_OP_MODE::DEV ? (int32_t)args.GetArg("-blockfeatures", ConsensusConstants::TAPYRUS_BLOCK_FEATURES) : ConsensusConstants::TAPYRUS_BLOCK_FEATURES),
      expectedBlockTime(ParseInRange(args, "expectedblocktime", DEFAULT_EXPECTED_BLOCK_TIME, 1, std::numeric_limits<uint32_t>::max()))
{
}
