// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <checkblock.h>

#include <consensus/merkle.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <heightderivedconsensusrules.h>
#include <primitives/block.h>
#include <pubkey.h>
#include <tinyformat.h>

bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, const HeightDerivedConsensusRules& consensusAtHeight, bool fCheckPOW)
{
    //check block features
    if(block.nFeatures != ConsensusConstants::TAPYRUS_BLOCK_FEATURES)
        return state.Invalid(false, REJECT_INVALID, "bad-features", "Incorrect Block features");

    //check xfieldType and xfield fields in the block header. Do not accept a block with unexpected xfieldType
    if(!block.xfield.IsValid())
        return state.Invalid(false, REJECT_INVALID, "bad-xfieldType-xfield", "Invalid xfieldType or xfield");

    if(!fCheckPOW)
        return true;

    // Schnorr proofs are always exactly 64 bytes. Reject early — before the
    // expensive GetHashForSign() + Verify_Schnorr call — if the proof is the
    // wrong size. A NONE xfield with trailing bytes appended by a peer causes
    // those bytes to be consumed as proof data, so wrong-size proofs are the
    // primary symptom of that malformation.
    if (block.proof.size() != CPubKey::SCHNORR_SIGNATURE_SIZE)
        return state.Invalid(false, REJECT_INVALID, "bad-proof-size",
            strprintf("Proof must be %u bytes, got %u",
                      CPubKey::SCHNORR_SIGNATURE_SIZE, block.proof.size()));

    // The aggregate key is the one at the block's height, not at the chain tip.
    const uint256 blockHash = block.GetHashForSign();
    if(!consensusAtHeight.AggregatePubKey().Verify_Schnorr(blockHash, block.proof))
        return state.Invalid(false, REJECT_INVALID, "bad-proof", strprintf("Proof verification failed at height [%s]",
                             consensusAtHeight.Height() ? std::to_string(*consensusAtHeight.Height()) : "unknown"));

    return true;
}

bool CheckBlock(const CBlock& block, CValidationState& state, const HeightDerivedConsensusRules& consensusAtHeight, bool fCheckPOW, bool fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check the merkle root.
    if (fCheckMerkleRoot) {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, false, REJECT_INVALID, "bad-txnmrklroot", true, "hashMerkleRoot mismatch");

        uint256 hashImMerkleRoot2 = BlockMerkleRoot(block, &mutated, true);

        if (block.hashImMerkleRoot != hashImMerkleRoot2)
            return state.DoS(100, false, REJECT_INVALID, "bad-txnimmrklroot", true, "hashImMerkleRoot mismatch");

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-duplicate", true, "duplicate transaction");
    }

    // First transaction must be coinbase,
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing", false, "first tx is not coinbase");

    // coinbase should not have colored output
    for(auto txOut: block.vtx[0]->vout)
        if(txOut.scriptPubKey.IsColoredScript())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-issuetoken", false, "coinbase cannot issue tokens");

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits: the limit at this block's position, which can differ from the tip's during reindex.
    uint32_t serializedSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    if (block.vtx.empty() || block.vtx.size() > consensusAtHeight.MaxBlockSize() || serializedSize > consensusAtHeight.MaxBlockSize()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false, "size limits failed");
    }

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, consensusAtHeight, fCheckPOW))
        return false;

    //the rest must not be coinbase
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i]->IsCoinBase())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-multiple", false, "more than one coinbase");

    // Check transactions
    for (const auto& tx : block.vtx)
    {
        // Each transaction must fit in this block's own size limit, not the latest one.
        if (!CheckTransaction(*tx, state, consensusAtHeight.MaxBlockSize(), true))
            return state.Invalid(false, state.GetRejectCode(), state.GetRejectReason(),
                                 strprintf("Transaction check failed (tx hash %s) %s", tx->GetHashMalFix().ToString(), state.GetDebugMessage()));
    }
    unsigned int nSigOps = 0;
    for (const auto& tx : block.vtx)
    {
        nSigOps += GetLegacySigOpCount(*tx);
    }
    if (nSigOps > consensusAtHeight.MaxBlockSigops())
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops", false, strprintf("out-of-bounds SigOpCount [%d]", nSigOps));

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}
