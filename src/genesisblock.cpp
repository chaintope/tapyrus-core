// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <genesisblock.h>

#include <amount.h>
#include <consensus/merkle.h>
#include <key_io.h>
#include <primitives/xfield.h>
#include <script/script.h>
#include <script/standard.h>
#include <tinyformat.h>

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
    if (payToaddress.empty()) {
        txNew.vout[0].scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(aggregatePubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    } else {
        CTxDestination dest = DecodeDestination(payToaddress);
        if (!IsValidDestination(dest))
            throw std::runtime_error(strprintf("createGenesisBlock: invalid -address: %s", payToaddress));
        txNew.vout[0].scriptPubKey = GetScriptForDestination(dest);
    }

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
