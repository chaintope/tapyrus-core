// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <checkpoints.h>

#include <blockfilemanager.h>
#include <blockindex.h>
#include <chainstatemanager.h>
#include <consensussettings.h>
#include <validation.h>

#include <stdint.h>

namespace Checkpoints {

    CBlockIndex* GetLastCheckpoint(BlockIndexMap& blockIndex, const CCheckpointData& data)
    {
        AssertLockHeld(cs_main);
        const MapCheckpoints& checkpoints = data.mapCheckpoints;

        for (auto it = checkpoints.rbegin(); it != checkpoints.rend(); ++it) {
            const MapCheckpoints::value_type& i = *it;
            const uint256& hash = i.second;
            CBlockIndex* pindex = blockIndex.LookupBlockIndex(hash);
            if (pindex) {
                return pindex;
            }
        }
        return nullptr;
    }

} // namespace Checkpoints
