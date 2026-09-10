// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <dynamicparams.h>

#include <primitives/xfield.h>
#include <pubkey.h>
#include <streams.h>
#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>
#include <xfieldhistory.h>

#include <assert.h>
#include <fstream>

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

CDynamicParams::CDynamicParams(const std::string& genesisHex)
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
            } else {
                throw std::runtime_error(strprintf("Unknown public key prefix 0x%02x in genesis block: %s", (*pubkey)[0], HexStr(*pubkey)));
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
}

static std::unique_ptr<CDynamicParams> globalDynamicParams;

const CDynamicParams& DynamicParams()
{
    assert(globalDynamicParams);
    return *globalDynamicParams;
}

std::unique_ptr<CDynamicParams> CreateDynamicParams(const std::string& genesisHex)
{
    return std::unique_ptr<CDynamicParams>(new CDynamicParams(genesisHex));
}

void SelectDynamicParams(const std::string& genesisHex)
{
    globalDynamicParams = CreateDynamicParams(genesisHex);
}
