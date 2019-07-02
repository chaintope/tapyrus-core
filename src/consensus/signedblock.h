// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CONSENSUS_SIGNEDBLOCK_H
#define TAPYRUS_CONSENSUS_SIGNEDBLOCK_H

#include <pubkey.h>

const unsigned int SIGNED_BLOCKS_MAX_KEY_SIZE = 15;
/* MultisigCondition:
singleton signedblock condition
instance can be accessed using
MultisigCondition::getInstance() or CChainParams::getSignedBlocksCondition()
*/
struct MultisigCondition {
    MultisigCondition(const std::string& pubkeyString, const int threshold);
    static const MultisigCondition& getInstance();
    void ParsePubkeyString(std::string source);
    bool operator==(const MultisigCondition& rhs) const {
        return (instance && instance->pubkeys == rhs.pubkeys && instance->threshold == rhs.threshold);
    }

    uint8_t getThreshold() const{
        return instance->threshold;
    }
    const std::vector<CPubKey>& getPubkeys() const{
        return instance->pubkeys;
    }
private:
    static std::unique_ptr<MultisigCondition> instance;

    std::vector<CPubKey> pubkeys;
    uint8_t threshold;

    MultisigCondition() {}
    friend struct ChainParamsTestingSetup;
};

#endif // TAPYRUS_CONSENSUS_SIGNEDBLOCK_H