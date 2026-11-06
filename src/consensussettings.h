// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CONSENSUSSETTINGS_H
#define TAPYRUS_CONSENSUSSETTINGS_H

#include <consensusconstants.h>
#include <softforkmanager.h>
#include <uint256.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class CBlock;
enum class TAPYRUS_OP_MODE;

typedef std::map<int, uint256> MapCheckpoints;

struct CCheckpointData {
    MapCheckpoints mapCheckpoints;
};

/**
 * The values every node on a network must agree on to accept the same blocks.
 * The constants come from ConsensusConstants; the rest is built once at start-up
 * from the genesis block, the operating mode and the network id, and never changes.
 */
class ConsensusSettings : public ConsensusConstants
{
public:
    /**
     * Deserialises the genesis block and checks it, including its proof.
     * @throws std::runtime_error if the genesis block is not valid.
     */
    ConsensusSettings(const std::vector<unsigned char>& genesis, TAPYRUS_OP_MODE mode, uint32_t networkId);
    ~ConsensusSettings();

    ConsensusSettings(const ConsensusSettings&) = delete;
    ConsensusSettings& operator=(const ConsensusSettings&) = delete;

    const CBlock& GenesisBlock() const { return *m_genesis; }
    uint32_t NetworkId() const { return m_network_id; }
    int32_t SubsidyHalvingInterval() const { return m_subsidy_halving_interval; }
    /** The coinbase subsidy of the block at this height, in tapyrus. */
    int64_t BlockSubsidy(int32_t height) const;
    const CCheckpointData& Checkpoints() const { return m_checkpoints; }
    /** The softforks registered for this network. */
    const CSoftForkManager& SoftForkManager() const { return m_soft_forks; }

protected:
    //! Lets tests use a halving interval that neither mode has.
    ConsensusSettings(const std::vector<unsigned char>& genesis, TAPYRUS_OP_MODE mode, uint32_t networkId, int32_t subsidyHalvingInterval);

private:
    const std::unique_ptr<const CBlock> m_genesis;
    const uint32_t m_network_id;
    const int32_t m_subsidy_halving_interval;
    const CCheckpointData m_checkpoints;
    const CSoftForkManager m_soft_forks;
};

/**
 * The node's consensus settings, set once at start-up. It is a global until
 * ConsensusInterface is implemented, and then moves inside ConsensusInterface.
 */
extern std::unique_ptr<const ConsensusSettings> g_consensus_settings;

#endif // TAPYRUS_CONSENSUSSETTINGS_H
