// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERIFYDB_H
#define BITCOIN_VERIFYDB_H

#include <coinsview.h>

class Chainstate;

/** RAII wrapper for VerifyDB: Verify consistency of the block and coin databases */
class CVerifyDB {
public:
    CVerifyDB();
    ~CVerifyDB();
    /** Checks the last nCheckDepth blocks of chainstate's active chain at nCheckLevel, reading coins through coinsview. */
    bool VerifyDB(Chainstate& chainstate, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth);
    //! On the active chainstate; goes with g_chainstate_manager.
    bool VerifyDB(CCoinsView *coinsview, int nCheckLevel, int nCheckDepth);
};

#endif //BITCOIN_VERIFYDB_H