// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FILE_IO_H
#define BITCOIN_FILE_IO_H

class CTxMemPool;

constexpr size_t REINDEX_BUFFER_SIZE = 32 * 1000000;  //  use large 32MB buffer to handle any block size

/** Dump the mempool to disk. */
bool DumpMempool(const CTxMemPool& pool);

#endif // BITCOIN_FILE_IO_H