// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <federationparams.h>

#include <tinyformat.h>
#include <util.h>
#include <utilmemory.h>
#include <utilstrencodings.h>
#include <consensus/merkle.h>
#include <key_io.h>
#include <tapyrusmodes.h>
#include <validation.h>
#include <xfieldhistory.h>

#include <assert.h>
#include <fstream>

void SetupFederationParamsOptions()
{
    gArgs.AddArg("-dev", "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
                                   "This is intended for regression testing tools and app development.", true, OptionsCategory::CHAINPARAMS);
}

std::string ReadGenesisBlock(fs::path genesisPath)
{
    std::string genesisFileName(TAPYRUS_GENESIS_FILENAME);

    //if network id was passed read genesis.<networkid>
    if(gArgs.IsArgSet("-networkid"))
        genesisFileName.replace(8, 3, std::to_string(gArgs.GetArg("-networkid", 0)));
    genesisPath /= genesisFileName;

    LogPrintf("Reading Genesis Block from [%s]\n", genesisPath.string().c_str());
    std::ifstream stream(genesisPath.string());
    if (!stream.good())
        throw std::runtime_error(strprintf("ReadGenesisBlock: unable to read genesis file %s", genesisPath));

    std::string genesisHex;
    stream >> genesisHex;
    stream.close();

    return genesisHex;
}

CBlock createGenesisBlock(const CPubKey& aggregatePubkey, const CKey& privateKey, const time_t blockTime, std::string payToaddress)
{
    //Genesis coinbase transaction paying block reward to the first public key in signedBlocksCondition
    CMutableTransaction txNew;
    txNew.nFeatures = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].prevout.n = 0;
    txNew.vin[0].scriptSig = CScript();
    txNew.vout[0].nValue = 50 * COIN;
    //if payToaddress is invalid, pay to agg pubkey
    if(payToaddress.empty() || !IsValidDestination(DecodeDestination(payToaddress)))
        txNew.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(aggregatePubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    else
        txNew.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(payToaddress) << OP_EQUALVERIFY << OP_CHECKSIG;

    //Genesis block header
    CBlock genesis;
    genesis.nTime    = blockTime;
    genesis.nFeatures = 1;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashImMerkleRoot = BlockMerkleRoot(genesis, nullptr, true);
    genesis.xfield.xfieldType = TAPYRUS_XFIELDTYPES::AGGPUBKEY;
    genesis.xfield.xfieldValue = XFieldAggPubKey(std::vector<unsigned char>(aggregatePubkey.begin(), aggregatePubkey.end()));

    //Genesis block proof
    uint256 blockHash = genesis.GetHashForSign();
    std::vector<unsigned char> vchSig;
    if( privateKey.IsValid())
    {
        privateKey.Sign_Schnorr(blockHash, vchSig);

        if(vchSig.size() != CPubKey::SCHNORR_SIGNATURE_SIZE
        || !aggregatePubkey.Verify_Schnorr(blockHash, vchSig))
            vchSig.clear();

        //add signatures to genesis block
        genesis.proof.clear();
        genesis.proof = std::move(vchSig);
    }
    return genesis;
}

static std::unique_ptr<CFederationParams> globalChainFederationParams;

const CFederationParams& FederationParams()
{
    assert(globalChainFederationParams);
    return *globalChainFederationParams;
}

std::unique_ptr<CFederationParams> CreateFederationParams(const TAPYRUS_OP_MODE mode, const bool withGenesis)
{
    gArgs.SelectConfigNetwork(TAPYRUS_MODES::GetChainName(mode));

    int64_t nid;
    bool inRange = gArgs.IsGetArgInRange("-networkid", 1, UINT_MAX,  TAPYRUS_MODES::GetDefaultNetworkId(mode), nid);
    if(!inRange || nid <= 0)
        throw std::runtime_error(strprintf("Network Id [%ld] was out of range. Expected range is 1 to 4294967295.", nid));
    const uint32_t networkId = (uint32_t)nid;
    const std::string dataDirName(GetDataDirNameFromNetworkId(networkId));
    const std::string genesisHex(withGenesis ? ReadGenesisBlock() : "");

    return MakeUnique<CFederationParams>(networkId, dataDirName, genesisHex);
}

void SelectFederationParams(const TAPYRUS_OP_MODE mode, const bool withGenesis)
{
    globalChainFederationParams = CreateFederationParams(mode, withGenesis);
}

CFederationParams::CFederationParams(const uint32_t networkId, const std::string dataDirName, const std::string genesisHex) : nNetworkId(networkId), strNetworkID(std::to_string(networkId)), dataDir(dataDirName) {

    /**
     * The message start string is designed to be unlikely to occur in normal data.
     * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
     * a large 32-bit integer with any alignment.
     *
     * tapyrus message start string is 0x01 0xFF 0xF0 0x00.
     * testnet message start string is 0x75 0x9A 0x83 0x74. it is xor of mainnet header and testnet ascii codes.
     */

    int magicBytes = 33550335 + nNetworkId;
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << magicBytes;
    pchMessageStart[0] = stream[3];
    pchMessageStart[1] = stream[2];
    pchMessageStart[2] = stream[1];
    pchMessageStart[3] = stream[0];

    if(genesisHex.size())
        this->ReadGenesisBlock(genesisHex);

    vSeeds = gArgs.GetArgs("-addseeder");
}

bool CFederationParams::ReadGenesisBlock(std::string genesisHex)
{
    CDataStream ss(ParseHex(genesisHex), SER_NETWORK, PROTOCOL_VERSION);
    unsigned long streamsize = ss.size();
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
            }
        }
        break;
        case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
        BOOST_FALLTHROUGH;
        case TAPYRUS_XFIELDTYPES::NONE:
        BOOST_FALLTHROUGH;
        default:
            throw std::runtime_error("ReadGenesisBlock: invalid xfieldType in genesis block");
    }

    /* Performing non trivial validation here.
    * full block validation will be done later in ConnectBlock
    */
    if(ss.size() || genesisHex.length() != streamsize * 2)
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

    //initialize xfield history
    CXFieldHistory history(genesis);
    return true;
}

