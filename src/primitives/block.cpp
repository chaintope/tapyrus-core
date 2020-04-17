// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <utilstrencodings.h>
#include <crypto/common.h>
#include <chainparams.h>

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

std::string CBlockHeader::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlockHeader(nFeatures=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, hashImMerkleRoot=%s, nTime=%u, xType=%2x, xValue=%s, proof={%s})) hash=%s",
                   nFeatures,
                   hashPrevBlock.ToString(),
                   hashMerkleRoot.ToString(),
                   hashImMerkleRoot.ToString(),
                   nTime,
                   xType,
                   HexStr(xValue),
                   HexStr(proof),
                   GetHash().ToString());
    return s.str();
}

uint256 CBlockHeaderWithoutProof::GetHashForSign() const
{
    return SerializeHash(*this);
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, nFeatures=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, hashImMerkleRoot=%s, nTime=%u, xType=%2x, xValue=%s, proof={%s} vtx=%u)\n",
        GetHash().ToString(),
        nFeatures,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        hashImMerkleRoot.ToString(),
        nTime,
        xType,
        HexStr(xValue),
        HexStr(proof),
        vtx.size());
        for (const auto& tx : vtx) {
            s << "  " << tx->ToString() << "\n";
        }
    return s.str();
}

bool CBlockHeader::AbsorbBlockProof(const std::vector<unsigned char>& blockproof, const CPubKey& aggregatePubkey)
{
    //using hash of block without proof for signing
    uint256 blockHash = this->GetHashForSign();

    if(blockproof.size() != CPubKey::SCHNORR_SIGNATURE_SIZE)
        return false;

    //verify signature
    if (!aggregatePubkey.Verify_Schnorr(blockHash, blockproof))
        return false;

    //clear old proof
    proof.clear();

    //add signatures to block
    proof = std::move(blockproof);
    return true;

}