// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_PRECOMPUTEDCOLORDATA_H
#define TAPYRUS_PRECOMPUTEDCOLORDATA_H

#include <amount.h>
#include <coloridentifier.h>
#include <cs_main.h>
#include <primitives/outpoint.h>
#include <sync.h>

#include <cstdint>
#include <vector>

class CCoinsViewCache;
class CIssuedColorIds;
class CTransaction;

/**
 * The colour data of one transaction, read once from its coin view so the
 * colour checks never touch the view or the issued-colorid store themselves.
 */
class PrecomputedColorData
{
public:
    struct Input {
        COutPoint prevout;
        CAmount value{0};
        //! NONE for TPC and for an OP_COLOR script that is not CP2PKH or CP2SH
        ColorIdentifier colorId;
        //! OP_COLOR script that is not CP2PKH or CP2SH
        bool nonstandardColored{false};
        bool spent{false};
        //! Reissuable colorid this TPC input can issue: the hash of its script
        ColorIdentifier scriptColorId;
        //! Payload of the non-reissuable or NFT colorid this TPC input can issue: the hash of its outpoint
        ColorIdentifier outpointColorId;
    };

    struct Output {
        CAmount value{0};
        ColorIdentifier colorId;
        //! Script contains OP_COLOR
        bool colored{false};
        //! Non-reissuable or NFT colorid already issued on chain
        bool issued{false};
    };

    PrecomputedColorData(const CTransaction& tx, const CCoinsViewCache& inputs, int32_t blockHeight);

    /** Mark the outputs whose non-reissuable or NFT colorid is already issued. */
    void SetIssued(const CIssuedColorIds& issued) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    const std::vector<Input>& Inputs() const { return m_inputs; }
    const std::vector<Output>& Outputs() const { return m_outputs; }
    bool IsCoinBase() const { return m_coinbase; }
    //! CP2SH colored softfork active at the block being checked
    bool Cp2shColoredActive() const { return m_cp2sh_colored_active; }

private:
    std::vector<Input> m_inputs;
    std::vector<Output> m_outputs;
    bool m_coinbase;
    bool m_cp2sh_colored_active;
};

#endif // TAPYRUS_PRECOMPUTEDCOLORDATA_H
