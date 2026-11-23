// Copyright (c) 2017 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_MINING_H
#define BITCOIN_RPC_MINING_H

#include <script/script.h>

#include <univalue.h>

class ChainstateManager;
class CTxMemPool;
struct PolicySettings;

/** Generate blocks (mine) */
UniValue generateBlocks(ChainstateManager& chainman, const PolicySettings& policy, std::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, bool keepScript, const CKey& vecPrivKeys);
//! On the active chain; goes with g_chainstate_manager.
UniValue generateBlocks(const CTxMemPool& mempool, std::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, bool keepScript, const CKey& vecPrivKeys);

/** Check bounds on a command line confirm target; max_target is the longest the fee estimator tracks */
unsigned int ParseConfirmTarget(const UniValue& value, unsigned int max_target);

/** parse list of privatekeys from unival hex string into a vector */
void ParsePrivateKeyList(const UniValue& privkeys_hex, std::vector<CKey>& vecKeys);

#endif
