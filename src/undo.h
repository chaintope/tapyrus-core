// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UNDO_H
#define BITCOIN_UNDO_H

#include <compressor.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <serialize.h>

/** Undo information for a CTxIn
 *
 *  Contains the prevout's CTxOut being spent, and its metadata as well
 *  (coinbase or not, height). The serialization contains a dummy value of
 *  zero. This is compatible with older versions which expect to see
 *  the transaction version there.
 */
class TxInUndoSerializer
{
    const Coin* txout;

public:
    template<typename Stream>
    void Serialize(Stream &s) const {
        ::Serialize(s, VARINT(txout->nHeight * 2 + (txout->fCoinBase ? 1u : 0u)));
        if (txout->nHeight > 0) {
            // Required to maintain compatibility with older undo format.
            ::Serialize(s, (unsigned char)0);
        }
        ::Serialize(s, CTxOutCompressor(REF(txout->out)));
    }

    explicit TxInUndoSerializer(const Coin* coin) : txout(coin) {}
};

class TxInUndoDeserializer
{
    Coin* txout;

public:
    template<typename Stream>
    void Unserialize(Stream &s) {
        unsigned int nCode = 0;
        ::Unserialize(s, VARINT(nCode));
        txout->nHeight = nCode / 2;
        txout->fCoinBase = nCode & 1;
        if (txout->nHeight > 0) {
            // Old versions stored the version number for the last spend of
            // a transaction's outputs. Non-final spends were indicated with
            // height = 0.
            unsigned int nVersionDummy{0};
            ::Unserialize(s, VARINT(nVersionDummy));
        }
        ::Unserialize(s, CTxOutCompressor(REF(txout->out)));
    }

    explicit TxInUndoDeserializer(Coin* coin) : txout(coin) {}
};

/** Undo information for a CTransaction */
class CTxUndo
{
public:
    // undo information for all txins
    std::vector<Coin> vprevout;

    template <typename Stream>
    void Serialize(Stream& s) const {
        // TODO: avoid reimplementing vector serializer
        uint64_t count = vprevout.size();
        ::Serialize(s, COMPACTSIZE(REF(count)));
        for (const auto& prevout : vprevout) {
            ::Serialize(s, TxInUndoSerializer(&prevout));
        }
    }
};

/** Undo information for a CBlock. Read it with BlockUndoDeserializer. */
class CBlockUndo
{
public:
    std::vector<CTxUndo> vtxundo; // for all but the coinbase

    template <typename Stream>
    void Serialize(Stream& s) const {
        ::Serialize(s, vtxundo);
    }
};

/** Reads a CBlockUndo for a known block. Every record count must match the
 *  block before anything is allocated, so a corrupt count in the undo file
 *  fails the read instead of causing a huge allocation. */
class BlockUndoDeserializer
{
    CBlockUndo* blockundo;
    const CBlock& block;

public:
    template<typename Stream>
    void Unserialize(Stream& s) {
        uint64_t tx_count = 0;
        ::Unserialize(s, COMPACTSIZE(tx_count));
        if (block.vtx.empty() || tx_count != block.vtx.size() - 1) {
            throw std::ios_base::failure("Undo transaction count does not match block");
        }
        blockundo->vtxundo.resize(tx_count);
        for (size_t i = 0; i < tx_count; ++i) {
            uint64_t count = 0;
            ::Unserialize(s, COMPACTSIZE(count));
            if (count != block.vtx[i + 1]->vin.size()) {
                throw std::ios_base::failure("Undo input count does not match transaction");
            }
            std::vector<Coin>& vprevout = blockundo->vtxundo[i].vprevout;
            vprevout.resize(count);
            for (auto& prevout : vprevout) {
                ::Unserialize(s, TxInUndoDeserializer(&prevout));
            }
        }
    }

    BlockUndoDeserializer(CBlockUndo* blockundoIn, const CBlock& blockIn) : blockundo(blockundoIn), block(blockIn) {}
};

#endif // BITCOIN_UNDO_H
