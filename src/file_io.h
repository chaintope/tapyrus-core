// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FILE_IO_H
#define BITCOIN_FILE_IO_H

constexpr size_t REINDEX_BUFFER_SIZE = 32 * 1000000;  //  use large 32MB buffer to handle any block size

/** Load the mempool from disk. */
bool LoadMempool();

/** Dump the mempool to disk. */
bool DumpMempool();

#endif // BITCOIN_FILE_IO_H