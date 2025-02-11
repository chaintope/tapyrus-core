// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_SCRIPTCHECK_H
#define TAPYRUS_SCRIPTCHECK_H

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
    ScriptError error;
    PrecomputedTransactionData *txdata;
    ColorIdentifier colorid;

public:
    CScriptCheck(): ptxTo(nullptr), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR), colorid(ColorIdentifier()) {}
    CScriptCheck(const CTxOut& outIn, const CTransaction& txToIn, unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn, PrecomputedTransactionData* txdataIn, ColorIdentifier coloridIn = ColorIdentifier()) :
        m_tx_out(outIn), ptxTo(&txToIn), nIn(nInIn), nFlags(nFlagsIn), cacheStore(cacheIn), error(SCRIPT_ERR_UNKNOWN_ERROR), txdata(txdataIn), colorid(coloridIn) { }

    bool operator()();

    ScriptError GetScriptError() const { return error; }
    const ColorIdentifier& GetColorIdentifier() const { return colorid; }
};

/** Initializes the script-execution cache */
void InitScriptExecutionCache();

#endif // TAPYRUS_SCRIPTCHECK_H