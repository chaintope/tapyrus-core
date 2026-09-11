// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CheckBlock()/CheckBlockHeader() live in their own translation unit,
// separate from validation.cpp, specifically so tapyrus-genesis can compile
// its own copy (see its add_executable() in the top-level src/CMakeLists.txt)
// without linking tapyrus_chainstate: validation.cpp is one translation unit
// shared by functions that touch g_chainstate/leveldb-backed disk storage
// (FlushStateToDisk, ReadBlockFromDisk, ...), and static archives are
// extracted per translation unit, not per function -- referencing anything
// in that file, including CheckBlock, pulls all of it in. Compiling this
// file's own copy directly into tapyrus-genesis (with TAPYRUS_GENESIS_SKIP
// defined) satisfies CheckBlock/CheckBlockHeader/isBlockHeightInCoinbase
// before the linker ever consults libtapyrus_chainstate.a, so that archive's
// copy of this same file (compiled without the macro, for every other
// consumer) is simply never extracted.
#include <validation.h>

#include <consensus/merkle.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <pubkey.h>
#include <tinyformat.h>
#include <xfieldhistory.h>

#ifndef TAPYRUS_GENESIS_SKIP
// tapyrus coinbase must have blockheight in the prevout.n. Only meaningful
// once a chain exists to compare against -- see the TAPYRUS_GENESIS_SKIP
// note on CheckBlock() below for why this is compiled out entirely for
// tapyrus-genesis, which calls CheckBlock() before any chain is loaded.
bool isBlockHeightInCoinbase(const CBlock& block)
{
    // when block and chainActive.Tip() are adjacent blocks we can compare and validate the block height.
    // otherwise we may be rewinding the block chain and they are unrelated blocks.
    CBlockIndex* pindex = chainActive.Tip();
    if(!pindex)
        return false;

    if(pindex->nHeight == 0)
        return true;

    uint32_t blockHeight = block.GetHeight();

    if(block.GetHash() == pindex->GetBlockHash() && blockHeight != (uint32_t)pindex->nHeight)
        return false;
    else if(block.GetBlockHeader().hashPrevBlock == pindex->GetBlockHash() && blockHeight != (uint32_t)pindex->nHeight + 1)
        return false;
    else if(pindex->GetBlockHeader().hashPrevBlock == block.GetHash() && blockHeight != (uint32_t)pindex->nHeight - 1)
        return false;

    else // if the two blocks are unrelated, we assume the block height is valid.
        return true;
}
#endif

std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)",
        state.GetRejectReason(),
        state.GetDebugMessage().empty() ? "" : ", "+state.GetDebugMessage(),
        state.GetRejectCode());
}

bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, CXFieldHistoryMap* pxfieldHistory, int nHeight, bool fCheckPOW)
{
    //check block features
    if(block.nFeatures != CBlock::TAPYRUS_BLOCK_FEATURES)
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

    // Aggpubkey to verify blocks is read from the xfield history at the block's
    // height, not at the chain tip. Both temp and non-temp paths now call Get(height) uniformly.
    // nHeight < 0 means the caller has no known block height (e.g. AcceptBlockHeader
    // with no pindexPrev, or ReadBlockFromDisk on an orphan child during reindex).
    // Use UINT32_MAX so Get() returns the latest rotation entry for those callers,
    // avoiding spurious bad-proof failures against post-rotation headers.
    // nHeight == 0 is the genesis block and must use height 0 (returns genesis key).
    const uint32_t uHeight = (nHeight >= 0) ? static_cast<uint32_t>(nHeight) : UINT32_MAX;
    XFieldAggPubKey aggregatePubkeyObj;
    if(pxfieldHistory)
        aggregatePubkeyObj = std::get<XFieldAggPubKey>(pxfieldHistory->Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, uHeight).xfieldValue);
    else
        aggregatePubkeyObj = std::get<XFieldAggPubKey>(CXFieldHistory().Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, uHeight).xfieldValue);
    CPubKey aggregatePubkey(aggregatePubkeyObj.getPubKey());

    const uint256 blockHash = block.GetHashForSign();

    //verify signature
    if(!aggregatePubkey.Verify_Schnorr(blockHash, block.proof))
        return state.Invalid(false, REJECT_INVALID, "bad-proof", strprintf("Proof verification failed at height [%d]", nHeight));

    return true;
}

bool CheckBlock(const CBlock& block, CValidationState& state, bool fCheckPOW, bool fCheckMerkleRoot, CXFieldHistoryMap* pxfieldHistory, int nHeight)
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

#ifndef TAPYRUS_GENESIS_SKIP
    // tapyrus coinbase must have blockheight in the prevout.n. Skipped
    // entirely under TAPYRUS_GENESIS_SKIP (see the file-level note
    // above): tapyrus-genesis calls CheckBlock() before any chain is loaded,
    // so chainActive.Tip() would be null and this is a no-op for it anyway --
    // but referencing chainActive at all forces g_chainstate's translation
    // unit (chainstate.cpp, leveldb-backed) into the link.
    CBlockIndex* pindexPrev = chainActive.Tip();
    if(pindexPrev && !isBlockHeightInCoinbase(block) )
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-invalid", false, "incorrect block height in coinbase");
#endif

    // Use the structurally-derived height for xfield policy lookups.
    // When nHeight == -1 (caller has no index context, e.g. the pre-lock belt-and-suspenders
    // check in ProcessNewBlock), fall back to UINT32_MAX so Get() returns the latest entry —
    // the same conservative behaviour as AcceptBlockHeader's secondary check.
    // Never use block.GetHeight() (coinbase prevout.n) for lookups: that field is
    // attacker-controlled and is only verified equal to the structural height for
    // blocks adjacent to the tip (see isBlockHeightInCoinbase).
    uint32_t height = (nHeight >= 0) ? static_cast<uint32_t>(nHeight) : UINT32_MAX;
    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits - use Get(height) to get the correct limit for this specific block height
    // This is important during reindex when processing blocks with different max block sizes
    XFieldMaxBlockSize maxBlockSizeChange;
    if(pxfieldHistory) {
        maxBlockSizeChange = std::get<XFieldMaxBlockSize>(pxfieldHistory->Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, height).xfieldValue);
    } else {
        maxBlockSizeChange = std::get<XFieldMaxBlockSize>(CXFieldHistory().Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, height).xfieldValue);
    }

    uint32_t serializedSize = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    if (block.vtx.empty() || block.vtx.size() > maxBlockSizeChange.data || serializedSize > maxBlockSizeChange.data) {
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false, "size limits failed");
    }

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, pxfieldHistory, height, fCheckPOW))
        return false;

    //the rest must not be coinbase
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i]->IsCoinBase())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-multiple", false, "more than one coinbase");

    // Check transactions
    for (const auto& tx : block.vtx)
    {
        if (!CheckTransaction(*tx, state, true))
            return state.Invalid(false, state.GetRejectCode(), state.GetRejectReason(),
                                 strprintf("Transaction check failed (tx hash %s) %s", tx->GetHashMalFix().ToString(), state.GetDebugMessage()));
    }
    unsigned int nSigOps = 0;
    for (const auto& tx : block.vtx)
    {
        nSigOps += GetLegacySigOpCount(*tx);
    }
    if (nSigOps > maxBlockSizeChange.GetMaxBlockSigops())
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops", false, strprintf("out-of-bounds SigOpCount [%d]", nSigOps));

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}
