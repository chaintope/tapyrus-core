// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utxo_snapshot.h>

#include <streams.h>
#include <utilstrencodings.h>


SnapshotMetadata::SnapshotMetadata(const uint256& base_blockhash, uint64_t coins_count) :
    base_blockhash(base_blockhash),
    coins_count(coins_count) { 
        ParseUInt64(FederationParams().NetworkIDString(), &networkid);
        network_mode = gArgs.GetChainMode();
}
