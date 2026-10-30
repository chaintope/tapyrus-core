// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_CONSENSUSCONSTANTS_H
#define TAPYRUS_CONSENSUSCONSTANTS_H

#include <cstdint>

/**
 * The consensus values fixed in code. ConsensusSettings derives from this;
 * the type headers include only this header, so they need no settings object.
 */
struct ConsensusConstants
{
    /** Amounts, in tapyrus (CAmount). */
    static constexpr int64_t COIN = 100000000;
    /**
     * No amount larger than this is valid. It is a sanity bound used by the
     * validation code, not the money supply, but changing it could fork the chain.
     */
    static constexpr int64_t MAX_MONEY = 21000000 * COIN;

    /** The maximum block size before any MAXBLOCKSIZE xfield change, in bytes. */
    static constexpr uint32_t MAX_BLOCK_SIZE = 1000000;
    /** Bytes of block size allowed per signature operation. */
    static constexpr uint32_t BLOCK_SIZE_PER_SIGOP = 50;
    /** The only valid value of a block's nFeatures. */
    static constexpr int32_t TAPYRUS_BLOCK_FEATURES = 1;
    /** How far a block's time may be ahead of network-adjusted time, in seconds. */
    static constexpr int64_t MAX_FUTURE_BLOCK_TIME = 2 * 60 * 60;
    /** Blocks whose median time a new block's time must exceed. */
    static constexpr int32_t nMedianTimeSpan = 11;

    /** Lower bound for the size of a valid serialized CTransaction. */
    static constexpr uint32_t MIN_TRANSACTION_SIZE = 60;
    /** Lower bound for the size of a serialized CTransaction. */
    static constexpr uint32_t MIN_SERIALIZABLE_TRANSACTION_SIZE = 10;

    /** Interpret sequence numbers as relative lock-time constraints. */
    static constexpr uint32_t LOCKTIME_VERIFY_SEQUENCE = (1 << 0);
    /** Use GetMedianTimePast() instead of nTime for end point timestamp. */
    static constexpr uint32_t LOCKTIME_MEDIAN_TIME_PAST = (1 << 1);
    /** nLockTime below this is a block height, otherwise a UNIX time. */
    static constexpr uint32_t LOCKTIME_THRESHOLD = 500000000; // Tue Nov  5 00:53:20 1985 UTC

    /** CTxIn::nSequence on every input disables nLockTime. */
    static constexpr uint32_t SEQUENCE_FINAL = 0xffffffff;
    /** If set, CTxIn::nSequence is NOT interpreted as a relative lock-time (BIP 68). */
    static constexpr uint32_t SEQUENCE_LOCKTIME_DISABLE_FLAG = (1U << 31);
    /** If set, a relative lock-time is in units of 512 seconds, otherwise in blocks. */
    static constexpr uint32_t SEQUENCE_LOCKTIME_TYPE_FLAG = (1 << 22);
    /** Extracts a relative lock-time from CTxIn::nSequence. */
    static constexpr uint32_t SEQUENCE_LOCKTIME_MASK = 0x0000ffff;
    /** A time-based relative lock-time is shifted up by these bits: 512 = 2^9 seconds. */
    static constexpr int32_t SEQUENCE_LOCKTIME_GRANULARITY = 9;

    /** Maximum number of bytes pushable to the stack. */
    static constexpr uint32_t MAX_SCRIPT_ELEMENT_SIZE = 520;
    /** Maximum number of non-push operations per script. */
    static constexpr int32_t MAX_OPS_PER_SCRIPT = 201;
    /** Maximum number of public keys per multisig. */
    static constexpr int32_t MAX_PUBKEYS_PER_MULTISIG = 20;
    /** Maximum script length in bytes. */
    static constexpr int32_t MAX_SCRIPT_SIZE = 10000;
    /** Maximum number of values on the script interpreter stack. */
    static constexpr int32_t MAX_STACK_SIZE = 1000;
    /** The highest valid opcode, OP_COLOR. */
    static constexpr uint32_t MAX_OPCODE = 0xbc;
    // No fixed set of mandatory script flags: P2SH, STRICTENC, DERSIG, LOW_S,
    // NULLDUMMY, MINIMALDATA, CLTV and CSV are always enforced, and the
    // soft-fork flags a block needs depend on its height.

    /** Size of color identifier data in bytes. */
    static constexpr uint32_t COLOR_IDENTIFIER_SIZE = 33;

    /** The Chaintope testnet, whose CP2SH colored softfork activates late. */
    static constexpr uint32_t TESTNET_NETWORK_ID = 1939510133;
    /** The last legacy CP2SH colored spend on that testnet was at block 693366. */
    static constexpr int32_t TESTNET_CP2SH_ACTIVATION_HEIGHT = 693367;
};

#endif // TAPYRUS_CONSENSUSCONSTANTS_H
