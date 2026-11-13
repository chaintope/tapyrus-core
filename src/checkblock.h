// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CHECKBLOCK_H
#define TAPYRUS_CHECKBLOCK_H

class CBlock;
class CBlockHeader;
class CValidationState;
class HeightDerivedConsensusRules;

/** Header validity checks that need only the block's rules, not the chain */
bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, const HeightDerivedConsensusRules& consensusAtHeight, bool fCheckPOW = true);

/** Block validity checks that need only the block's rules, not the chain */
bool CheckBlock(const CBlock& block, CValidationState& state, const HeightDerivedConsensusRules& consensusAtHeight, bool fCheckPOW = true, bool fCheckMerkleRoot = true);

#endif // TAPYRUS_CHECKBLOCK_H
