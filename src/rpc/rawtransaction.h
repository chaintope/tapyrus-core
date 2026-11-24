// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_RAWTRANSACTION_H
#define BITCOIN_RPC_RAWTRANSACTION_H

#include <script/standard.h>

#include <cstdint>
#include <map>
#include <vector>

class CBasicKeyStore;
class Coin;
class COutPoint;
class CTxIn;
class CTxOut;
struct CMutableTransaction;
class UniValue;
enum class SignatureScheme;

/** Sign a transaction with the given keystore, against coins holding the outputs it spends. Previous
 *  outputs given in prevTxs are added to coins. */
UniValue SignTransaction(CMutableTransaction& mtx, const UniValue& prevTxs, CBasicKeyStore *keystore, std::map<COutPoint, Coin>& coins, bool tempKeystore, const UniValue& hashType, const SignatureScheme sigScheme);

/** Build the output for one destination and amount; shared by ConstructTransaction and the PSTT output RPCs */
CTxOut BuildDestinationTxOut(const CTxDestination& destination, const UniValue& value);

/** Create a transaction from univalue parameters */
CMutableTransaction ConstructTransaction(const UniValue& inputs_in, const UniValue& outputs_in, const UniValue& locktime, const UniValue& rbf);

/** Parses a PSTT-shaped inputs array (txid/vout/sequence? keys, same
 *  vocabulary as createrawtransaction/ConstructTransaction -- see
 *  doc/tapyrus/pstt.md) into CTxIns. Used by createpstt and
 *  walletcreatefundedpstt instead of passing their inputs through
 *  ConstructTransaction directly, since that also accepts outputs/locktime
 *  parameters these two RPCs don't take in the same shape. */
std::vector<CTxIn> ParsePsttInputEntries(const UniValue& inputs_in, uint32_t nLockTime, bool rbfOptIn);

#endif // BITCOIN_RPC_RAWTRANSACTION_H
