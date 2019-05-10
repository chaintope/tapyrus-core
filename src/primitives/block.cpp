// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
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
    s << strprintf("CBlockHeader(ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, hashImMerkleRoot=%s, nTime=%u)) hash=%s",
                   nVersion,
                   hashPrevBlock.ToString(),
                   hashMerkleRoot.ToString(),
                   hashImMerkleRoot.ToString(),
                   nTime,
                   GetHash().ToString());
    return s.str();
}

uint256 CBlockHeaderWithoutProof::GetHashForSign() const
{
    return SerializeHash(*this);
}

std::string CBlock::ToString() const
{
    // TODO: Make output string includes proof field
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, hashImMerkleRoot=%s, nTime=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        hashImMerkleRoot.ToString(),
        nTime,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}

bool CBlockHeader::AbsorbBlockProof(CProof blockproof, const MultisigCondition& signedBlocksCondition)
{
    //using hash of block without proof for signing
    uint256 blockHash = this->GetHashForSign();

    //clear old proof
    proof.clear();

    unsigned int inProofSize = blockproof.size();

    if(!inProofSize)
        return false;

    //evaluate and sort blockProof signatures in the order of their corresponding public keys
    for(auto &pubkey: signedBlocksCondition.pubkeys)
    {
        CProof::iterator iter = blockproof.begin();
        for (; iter != blockproof.end(); iter++)
        {
            //verify signature
            if (pubkey.Verify(blockHash, *iter))
            {
                //add signatures to block
                proof.emplace_back(std::move(*iter));
                break;
            }
        }
        if(iter != blockproof.end())
            blockproof.erase(iter);

        if(!blockproof.size())
            break;
    }
    return proof.size() == inProofSize;
}