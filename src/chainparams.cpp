// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <assert.h>

#include <chainparamsseeds.h>
#include <streams.h>
#include <fs.h>

const MultisigCondition CreateSignedBlockCondition()
{
    const std::string pubkeys = gArgs.GetArg("-signblockpubkeys", "");
    const int threshold = std::stoi(gArgs.GetArg("-signblockthreshold", "0"));

    MultisigCondition signedBlockCondition(pubkeys, threshold);

    return signedBlockCondition;
}

void MultisigCondition::ParsePubkeyString(std::string source)
{
    std::string prefix;
    std::string pubkeyString;

    pubkeys.clear();

    for (unsigned int i = 0; i < source.length();) {
        prefix = source.substr(i, 2);

        if (prefix == "02" || prefix == "03") {
            pubkeyString = source.substr(i, 66);
            i += 66;
        } else if(prefix == "04" || prefix == "06" || prefix == "07") {
            throw std::runtime_error(strprintf("Uncompressed public key format are not acceptable: %s", source));
        } else {
            throw std::runtime_error(strprintf("Public Keys for Signed Block include invalid pubkey: %s", pubkeyString));
        }

        std::vector<unsigned char> vch = ParseHex(pubkeyString);
        CPubKey pubkey(vch.begin(), vch.end());

        if(!pubkey.IsFullyValid()) {
            throw std::runtime_error(strprintf("Public Keys for Signed Block include invalid pubkey: %s", pubkeyString));
        }

        pubkeys.push_back(pubkey);
    }

    // sort as ascending order
    std::sort(pubkeys.begin(), pubkeys.end());
}

MultisigCondition::MultisigCondition(const std::string& pubkeyString, const int threshold): threshold(threshold)
{
    if(pubkeys.size() && threshold && (unsigned long)threshold <= pubkeys.size() && pubkeys.size() <= SIGNED_BLOCKS_MAX_KEY_SIZE)
        return;

    ParsePubkeyString(pubkeyString);

    if (!pubkeys.size()) {
        throw std::runtime_error(strprintf("Invalid or empty publicKeyString"));
    }

    if (pubkeys.size() > SIGNED_BLOCKS_MAX_KEY_SIZE) {
        throw std::runtime_error(strprintf("Public Keys for Signed Block are up to %d, but passed %d.", SIGNED_BLOCKS_MAX_KEY_SIZE, pubkeys.size()));
    }

    if (threshold < 1 || (unsigned int)threshold > pubkeys.size()) {
        throw std::runtime_error(strprintf("Threshold can be between 1 to %d, but passed %d.", pubkeys.size(), threshold));
    }
}

bool CChainParams::ReadGenesisBlock(std::string genesisHex)
{
    CDataStream ss(ParseHex(genesisHex), SER_NETWORK, PROTOCOL_VERSION);
    unsigned long streamsize = ss.size();
    ss >> genesis;

    /* Performing non trivial validation here.
    * full block validation will be done later in ConnectBlock
    */
    if(ss.size() || genesisHex.length() != streamsize * 2)
        throw std::runtime_error("ReadGenesisBlock: invalid genesis file");

    if(!genesis.vtx.size() || genesis.vtx.size() > 1)
        throw std::runtime_error("ReadGenesisBlock: invalid genesis block");

    if(!genesis.proof.size() || genesis.proof.size() < signedBlocksCondition.threshold)
        throw std::runtime_error("ReadGenesisBlock: invalid genesis block");

    CTransactionRef genesisCoinbase(genesis.vtx[0]);
    if(!genesisCoinbase->IsCoinBase())
        throw std::runtime_error("ReadGenesisBlock: invalid genesis block");

    if(genesisCoinbase->vin[0].prevout.n)
        throw std::runtime_error("ReadGenesisBlock: invalid height in genesis block");

    if(genesis.hashMerkleRoot != genesisCoinbase->GetHash() 
    || genesis.hashImMerkleRoot != genesisCoinbase->GetHashMalFix())
        throw std::runtime_error("ReadGenesisBlock: invalid MerkleRoot in genesis block");

    consensus.hashGenesisBlock = genesis.GetHash();
    return true;
}

bool CChainParams::SetSignedBlocksCondition(const MultisigCondition condition) {
    signedBlocksCondition = condition;
    return true;
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams():CChainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 210000;

        consensus.nExpectedBlockTime = 15; // 15 sec
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1510704000; // November 15th, 2017.

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         *
         * tapyrus message start string is 0x01 0xFF 0xF0 0x00.
         * testnet message start string is 0x75 0x9A 0x83 0x74. it is xor of mainnet header and testnet ascii codes.
         */
        pchMessageStart[0] = 0x01;
        pchMessageStart[1] = 0xff;
        pchMessageStart[2] = 0xf0;
        pchMessageStart[3] = 0x00;
        nDefaultPort = 2357;  // 2357 is beautiful prime.
        nPruneAfterHeight = 100000;

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        // TODO: MUST change to seeder that is different of testnet. (ex: seed.tapyrus.chaintope.com, seed.tapyrus.com)
        vSeeds.emplace_back("seed.tapyrus.dev.chaintope.com");
        vSeeds.emplace_back("static-seed.tapyrus.dev.chaintope.com");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "bc";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        /* disable fallback fee on mainnet */
        m_fallback_fee_enabled = false;
    }
};


/**
 * Paradium Main network
 */
class CParadiumParams : public CChainParams {
public:
    CParadiumParams():CChainParams() {
        strNetworkID = "paradium";
        consensus.nSubsidyHalvingInterval = 210000;

        consensus.nExpectedBlockTime = 15; // 15 sec
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1510704000; // November 15th, 2017.

        /**
         * Paradium networkId is 101.
         * networkId=1 magic bytes is: 01 FF F0 00.
         * thus, paradium magic byes is: 01 FF F0 64.
         */
        pchMessageStart[0] = 0x01;
        pchMessageStart[1] = 0xff;
        pchMessageStart[2] = 0xf0;
        pchMessageStart[3] = 0x64;
        nDefaultPort = 2357;  // 2357 is beautiful prime.
        nPruneAfterHeight = 100000;

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        // TODO: MUST change to seeder that is different of testnet. (ex: seed.tapyrus.chaintope.com, seed.tapyrus.com)
        vSeeds.emplace_back("seed.paradium.dev.chaintope.com");
        vSeeds.emplace_back("static-seed.paradium.dev.chaintope.com");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,0);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "bc";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        /* disable fallback fee on mainnet */
        m_fallback_fee_enabled = false;
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams():CChainParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210000;

        consensus.nExpectedBlockTime = 15; // 15 sec
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1462060800; // May 1st 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1493596800; // May 1st 2017

        pchMessageStart[0] = 0x75;
        pchMessageStart[1] = 0x9a;
        pchMessageStart[2] = 0x83;
        pchMessageStart[3] = 0x74;
        nDefaultPort = 12357;
        nPruneAfterHeight = 1000;

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("seed.tapyrus.dev.chaintope.com");
        vSeeds.emplace_back("static-seed.tapyrus.dev.chaintope.com");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tb";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        /* enable fallback fee on testnet */
        m_fallback_fee_enabled = true;
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams():CChainParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;

        consensus.nExpectedBlockTime = 15; // 15 sec
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1510704000; // November 15th, 2017.

        pchMessageStart[0] = 0x75;
        pchMessageStart[1] = 0x9a;
        pchMessageStart[2] = 0x83;
        pchMessageStart[3] = 0x74;
        nDefaultPort = 12383;
        nPruneAfterHeight = 1000;

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "bcrt";

        /* enable fallback fee on regtest */
        m_fallback_fee_enabled = true;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::PARADIUM)
        return std::unique_ptr<CChainParams>(new CParadiumParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}

bool SetSignedBlocksCondition(const MultisigCondition condition)
{
    globalChainParams->SetSignedBlocksCondition(condition);
}

bool ReadGenesisBlock(fs::path genesisPath)
{
    genesisPath /= TAPYRUS_GENESIS_FILENAME;

    LogPrintf("Reading Genesis Block from [%s]\n", genesisPath.string().c_str());
    fs::ifstream stream(genesisPath);
    if (!stream.good())
        throw std::runtime_error(strprintf("ReadGenesisBlock: unable to read genesis file %s", genesisPath));

    std::string genesisHex;
    stream >> genesisHex;
    stream.close();

    return globalChainParams->ReadGenesisBlock(genesisHex);
}

CBlock createGenesisBlock(const MultisigCondition& signedBlocksCondition, const std::vector<CKey>& privateKeys, const time_t blockTime)
{
    CPubKey combinedPubKey = PubKeyCombine(signedBlocksCondition.pubkeys);
    CPubKey rewardToPubKey(signedBlocksCondition.pubkeys[0]);

    //Genesis coinbase transaction paying block reward to the first public key in signedBlocksCondition
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].prevout.n = 0;
    txNew.vin[0].scriptSig = CScript() << CScriptNum(signedBlocksCondition.threshold) << std::vector<unsigned char>(combinedPubKey.data(), combinedPubKey.data() + CPubKey::COMPRESSED_PUBLIC_KEY_SIZE);
    txNew.vout[0].nValue = 50 * COIN;
    txNew.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(rewardToPubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    //Genesis block header
    CBlock genesis;
    genesis.nTime    = blockTime;
    genesis.nVersion = 1;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashImMerkleRoot = BlockMerkleRoot(genesis, nullptr, true);

    //Genesis block proof
    CProof genesisProof;
    uint256 blockHash = genesis.GetHashForSign();
    std::vector<unsigned char> vchSig;
    for(auto key : privateKeys) {
        key.Sign(blockHash, vchSig);
        genesisProof.addSignature(vchSig);
    }

    genesis.AbsorbBlockProof(genesisProof, signedBlocksCondition);

    return genesis;
}