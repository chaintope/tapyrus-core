// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_SCRIPTCHECK_H
#define TAPYRUS_SCRIPTCHECK_H

#include <primitives/transaction.h>

#include <optional>
#include <script/script_error.h>

/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction
 */
class CScriptCheck
{
private:
    CTxOut m_tx_out;
    const CTransaction *ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;

public:
    CScriptCheck(): ptxTo(nullptr), nIn(0), nFlags(0), cacheStore(false) {}
    CScriptCheck(const CTxOut& outIn, const CTransaction& txToIn, unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn) :
        m_tx_out(outIn), ptxTo(&txToIn), nIn(nInIn), nFlags(nFlagsIn), cacheStore(cacheIn) { }

    // Returns nullopt on success, or the ScriptError on failure.
    std::optional<ScriptError> operator()();
};

/** Initializes the script-execution cache */
void InitScriptExecutionCache();

#endif // TAPYRUS_SCRIPTCHECK_H
