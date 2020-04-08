// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
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
#include <chainparamsseeds.h>
#include <validation.h>

#include <assert.h>

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
    fs::ifstream stream(genesisPath);
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
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].prevout.n = 0;
    txNew.vin[0].scriptSig = CScript() << std::vector<unsigned char>(aggregatePubkey.data(), aggregatePubkey.data() + CPubKey::COMPRESSED_PUBLIC_KEY_SIZE);
    txNew.vout[0].nValue = 50 * COIN;
    //if payToaddress is invalid, pay to agg pubkey
    if(payToaddress.empty() || !IsValidDestination(DecodeDestination(payToaddress)))
        txNew.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(aggregatePubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    else
        txNew.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(payToaddress) << OP_EQUALVERIFY << OP_CHECKSIG;

    //Genesis block header
    CBlock genesis;
    genesis.nTime    = blockTime;
    genesis.nVersion = 1;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashImMerkleRoot = BlockMerkleRoot(genesis, nullptr, true);
    genesis.aggPubkey = std::vector<unsigned char>(aggregatePubkey.data(), aggregatePubkey.data() + CPubKey::COMPRESSED_PUBLIC_KEY_SIZE);

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

    const int networkId = gArgs.GetArg("-networkid", TAPYRUS_MODES::GetDefaultNetworkId(mode));
    const std::string dataDirName(GetDataDirNameFromNetworkId(networkId));
    const std::string genesisHex(withGenesis ? ReadGenesisBlock() : "");

    return MakeUnique<CFederationParams>(networkId, dataDirName, genesisHex);
}

void SelectFederationParams(const TAPYRUS_OP_MODE mode, const bool withGenesis)
{
    globalChainFederationParams = CreateFederationParams(mode, withGenesis);
}

CFederationParams::CFederationParams(const int networkId, const std::string dataDirName, const std::string genesisHex) : nNetworkId(networkId), strNetworkID(std::to_string(networkId)), dataDir(dataDirName) {

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
        ReadGenesisBlock(genesisHex);

    vSeeds = gArgs.GetArgs("-addseeder");

    vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));
}

CPubKey CFederationParams::ReadAggregatePubkey(const std::vector<unsigned char>& pubkey, uint height)
{
    if(!pubkey.size())
        throw std::runtime_error("Aggregate Public Key for Signed Block is empty");
    
    if (pubkey[0] == 0x02 || pubkey[0] == 0x03) {
        aggPubkeyAndHeight p;
        p.aggpubkey = CPubKey(pubkey.begin(), pubkey.end());
        p.height = height+1;

        aggregatePubkeyHeight.push_back(p);
        aggregatePubkey.push_back(p.aggpubkey);
        height.push_back(p.height);
        if(!p.aggpubkey.IsFullyValid()) {
            throw std::runtime_error(strprintf("Aggregate Public Key for Signed Block is invalid: %s", HexStr(pubkey)));
        }

        if (p.aggpubkey.size() != CPubKey::COMPRESSED_PUBLIC_KEY_SIZE) {
            throw std::runtime_error(strprintf("Aggregate Public Key for Signed Block is invalid: %s", HexStr(pubkey)));
        }
        return p.aggpubkey;

    } else if(pubkey[0] == 0x04 || pubkey[0] == 0x06 || pubkey[0] == 0x07) {
        throw std::runtime_error(strprintf("Uncompressed public key format are not acceptable: %s", HexStr(pubkey)));
    } else {
        throw std::runtime_error(strprintf("Aggregate Public Key for Signed Block is invalid: %s", HexStr(pubkey)));
    }
}

bool CFederationParams::ReadGenesisBlock(std::string genesisHex)
{
    CDataStream ss(ParseHex(genesisHex), SER_NETWORK, PROTOCOL_VERSION);
    unsigned long streamsize = ss.size();
    ss >> genesis;

    ReadAggregatePubkey(genesis.aggPubkey, 0);

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
    if(!aggregatePubkey.back().Verify_Schnorr(blockHash, genesis.proof))
        throw std::runtime_error("ReadGenesisBlock: Proof verification failed");

    return true;
}
