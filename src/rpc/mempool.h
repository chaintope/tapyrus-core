// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_RPC_MEMPOOL_H
#define TAPYRUS_RPC_MEMPOOL_H

class CTxMemPool;
class UniValue;

/** Mempool information to JSON */
UniValue mempoolInfoToJSON(CTxMemPool& pool);

/** Mempool to JSON */
UniValue mempoolToJSON(CTxMemPool& pool, bool fVerbose = false);

#endif // TAPYRUS_RPC_MEMPOOL_H
