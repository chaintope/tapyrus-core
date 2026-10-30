// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensussettings.h>

#include <primitives/block.h>
#include <primitives/xfieldvalues.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <streams.h>
#include <tapyrusmodes.h>
#include <tinyformat.h>
#include <utilstrencodings.h>
#include <version.h>

#include <assert.h>
#include <stdexcept>

std::unique_ptr<const ConsensusSettings> g_consensus_settings;

static std::unique_ptr<const CBlock> CheckGenesisBlock(const std::vector<unsigned char>& genesisBytes)
{
    CDataStream ss(genesisBytes, SER_NETWORK, PROTOCOL_VERSION);
    auto genesisPtr = std::make_unique<CBlock>();
    CBlock& genesis = *genesisPtr;
    ss >> genesis;
    CPubKey aggPubKeyToVerify;

    switch(genesis.xfield.xfieldType)
    {
        case TAPYRUS_XFIELDTYPES::AGGPUBKEY: {
            std::vector<unsigned char>* pubkey = &std::get<XFieldAggPubKey>(genesis.xfield.xfieldValue).data;
            if(!pubkey->size())
                throw std::runtime_error("Aggregate Public Key for Signed Block is empty");

            if ((*pubkey)[0] == 0x02 || (*pubkey)[0] == 0x03) {
                aggPubKeyToVerify = CPubKey(pubkey->begin(), pubkey->end());
                if(!aggPubKeyToVerify.IsFullyValid()) {
                    throw std::runtime_error(strprintf("Aggregate Public Key for Signed Block is invalid: %s", HexStr(aggPubKeyToVerify)));
                }

                if (aggPubKeyToVerify.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) {
                    throw std::runtime_error(strprintf("Aggregate Public Key for Signed Block is invalid: %s size was: %d", HexStr(aggPubKeyToVerify), aggPubKeyToVerify.size()));
                }
                break;

            } else if((*pubkey)[0] == 0x04 || (*pubkey)[0] == 0x06 || (*pubkey)[0] == 0x07) {
                throw std::runtime_error(strprintf("Uncompressed public key format are not acceptable: %s", HexStr(*pubkey)));
            } else {
                throw std::runtime_error(strprintf("Unknown public key prefix 0x%02x in genesis block: %s", (*pubkey)[0], HexStr(*pubkey)));
            }
        }
        break;
        case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
        case TAPYRUS_XFIELDTYPES::NONE:
        default:
            throw std::runtime_error("ReadGenesisBlock: invalid xfieldType in genesis block");
    }

    /* Performing non trivial validation here.
    * full block validation will be done later in ConnectBlock
    */
    if(ss.size())
        throw std::runtime_error("ReadGenesisBlock: invalid genesis file");

    if(!genesis.vtx.size() || genesis.vtx.size() > 1)
        throw std::runtime_error("ReadGenesisBlock: invalid genesis block");

    if(genesis.proof.size() != CPubKey::SCHNORR_SIGNATURE_SIZE)
        throw std::runtime_error("ReadGenesisBlock: invalid genesis block");

    CTransactionRef genesisCoinbase(genesis.vtx[0]);
    if(!genesisCoinbase->IsCoinBase())
        throw std::runtime_error("ReadGenesisBlock: invalid genesis block");

    if(genesisCoinbase->vin[0].prevout.n)
        throw std::runtime_error("ReadGenesisBlock: invalid height in genesis block");

    if(genesis.hashMerkleRoot != genesisCoinbase->GetHash()
    || genesis.hashImMerkleRoot != genesisCoinbase->GetHashMalFix())
        throw std::runtime_error("ReadGenesisBlock: invalid MerkleRoot in genesis block");

    //verify proof
    const uint256 blockHash = genesis.GetHashForSign();

    if(!aggPubKeyToVerify.Verify_Schnorr(blockHash, genesis.proof))
        throw std::runtime_error("ReadGenesisBlock: Proof verification failed");

    return genesisPtr;
}

static int32_t SubsidyHalvingIntervalFor(TAPYRUS_OP_MODE mode)
{
    switch(mode)
    {
        case TAPYRUS_OP_MODE::PROD: return 210000;
        case TAPYRUS_OP_MODE::DEV: return 150;
        default: throw std::runtime_error(strprintf("%s: Unknown mode.", __func__));
    }
}

static CSoftForkManager RegisterSoftForks(uint32_t networkId)
{
    CSoftForkManager softForks;
    // CI test builds define CP2SH_ACTIVATION_TEST_HEIGHT so the functional test
    // can cross the activation boundary without mining hundreds of thousands of
    // blocks. The override applies to whichever network the test node is running.
    //
    // Production builds register only the Chaintope testnet entry. Every other
    // network has no entry and therefore activates CP2SH at genesis
    // (CSoftForkManager::IsActive returns true when no entry is found).
#ifdef CP2SH_ACTIVATION_TEST_HEIGHT
    softForks.Register(CSoftFork(networkId, SCRIPT_VERIFY_CP2SH_COLORED,
                                 HeightActivation(CP2SH_ACTIVATION_TEST_HEIGHT)));
#else
    if (networkId == ConsensusSettings::TESTNET_NETWORK_ID) {
        softForks.Register(CSoftFork(networkId, SCRIPT_VERIFY_CP2SH_COLORED,
                                     HeightActivation(ConsensusSettings::TESTNET_CP2SH_ACTIVATION_HEIGHT)));
    }
#endif
    return softForks;
}

ConsensusSettings::ConsensusSettings(const std::vector<unsigned char>& genesis, TAPYRUS_OP_MODE mode, uint32_t networkId)
    : ConsensusSettings(genesis, mode, networkId, SubsidyHalvingIntervalFor(mode))
{
}

ConsensusSettings::ConsensusSettings(const std::vector<unsigned char>& genesis, TAPYRUS_OP_MODE mode, uint32_t networkId, int32_t subsidyHalvingInterval)
    : m_genesis(CheckGenesisBlock(genesis)),
      m_network_id(networkId),
      m_subsidy_halving_interval(subsidyHalvingInterval),
      m_checkpoints(),
      m_soft_forks(RegisterSoftForks(networkId))
{
}

ConsensusSettings::~ConsensusSettings() = default;

int64_t ConsensusSettings::BlockSubsidy(int32_t height) const
{
    const int32_t halvings = height / m_subsidy_halving_interval;
    // Force block reward to zero when right shift is undefined.
    if (halvings >= 64)
        return 0;

    int64_t subsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years.
    subsidy >>= halvings;
    return subsidy;
}

const CSoftForkManager& GetSoftForkManager()
{
    assert(g_consensus_settings);
    return g_consensus_settings->SoftForkManager();
}

bool CSoftForkManager::IsActive(unsigned int flag, int32_t blockHeight) const
{
    assert(g_consensus_settings);
    return IsActive(g_consensus_settings->NetworkId(), flag, blockHeight);
}
