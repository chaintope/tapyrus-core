// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <checkpoints.h>

#include <chain.h>
#include <chainparams.h>
#include <validation.h>

#include <stdint.h>
#include <ranges>

namespace Checkpoints {

    CBlockIndex* GetLastCheckpoint(const CCheckpointData& data)
    {
        AssertLockHeld(cs_main);
        const MapCheckpoints& checkpoints = data.mapCheckpoints;

        for (const MapCheckpoints::value_type& i : std::views::reverse(checkpoints))
        {
            const uint256& hash = i.second;
            CBlockIndex* pindex = LookupBlockIndex(hash);
            if (pindex) {
                return pindex;
            }
        }
        return nullptr;
    }

} // namespace Checkpoints
