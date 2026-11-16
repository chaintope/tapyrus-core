// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_HEIGHTDERIVEDCONSENSUSRULES_H
#define TAPYRUS_HEIGHTDERIVEDCONSENSUSRULES_H

#include <consensusconstants.h>
#include <pubkey.h>

#include <cstdint>
#include <optional>

/**
 * The consensus values that depend on where a block sits in the chain: its
 * maximum size, the aggregate key that signs it and the script flags of the
 * soft forks active at it. Immutable; built once per block and passed to the
 * rules by const reference. Tools that have no chain build it from plain values.
 */
class HeightDerivedConsensusRules
{
public:
    /** @param height  the block's height, or nullopt when it is not known */
    HeightDerivedConsensusRules(std::optional<int32_t> height, uint32_t maxBlockSize, const CPubKey& aggregatePubKey, uint32_t scriptFlags)
        : m_height(height), m_max_block_size(maxBlockSize), m_aggregate_pubkey(aggregatePubKey), m_script_flags(scriptFlags) {}

    std::optional<int32_t> Height() const { return m_height; }
    uint32_t MaxBlockSize() const { return m_max_block_size; }
    /** The maximum number of signature check operations in a block. */
    uint32_t MaxBlockSigops() const { return m_max_block_size / ConsensusConstants::BLOCK_SIZE_PER_SIGOP; }
    const CPubKey& AggregatePubKey() const { return m_aggregate_pubkey; }
    uint32_t ScriptFlags() const { return m_script_flags; }

private:
    const std::optional<int32_t> m_height;
    const uint32_t m_max_block_size;
    const CPubKey m_aggregate_pubkey;
    const uint32_t m_script_flags;
};

#endif // TAPYRUS_HEIGHTDERIVEDCONSENSUSRULES_H
