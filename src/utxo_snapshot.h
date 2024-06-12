// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UTXO_SNAPSHOT_H
#define BITCOIN_UTXO_SNAPSHOT_H

#include <cs_main.h>
#include <serialize.h>
#include <sync.h>
#include <uint256.h>
#include <fs.h>
#include <xfieldhistory.h>


// UTXO set snapshot magic bytes
static constexpr std::array<uint8_t, 5> SNAPSHOT_MAGIC_BYTES = {'u', 't', 'x', 'o', 0xff};

//! Metadata describing a serialized version of a UTXO set from which a  new chainstate can be constructed.
class SnapshotMetadata
{
    const uint16_t version{1};
    const std::set<uint16_t> supported_versions{1};

public:
    //! The network id associated with this snapshot
    uint64_t networkid;

    //! network mode to identify prod/dev network
    TAPYRUS_OP_MODE network_mode;

    //! The hash of the block that reflects the tip of the chain for the
    //! UTXO set contained in this snapshot.
    uint256 base_blockhash;

    //! The number of coins in the UTXO set contained in this snapshot.
    uint64_t coins_count = 0;

    SnapshotMetadata() { };

    SnapshotMetadata(const uint256& base_blockhash, uint64_t coins_count);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        s << SNAPSHOT_MAGIC_BYTES;
        s << version;
        s << networkid;
        s << TAPYRUS_MODES::GetChainName(network_mode);
        s << base_blockhash;
        s << VARINT(coins_count);
    }
};

#endif // BITCOIN_UTXO_SNAPSHOT_H
