// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2018-2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_ECCVERIFYHANDLE_H
#define TAPYRUS_ECCVERIFYHANDLE_H

#include <stdint.h>

typedef struct secp256k1_context_struct secp256k1_context;

/** Owns the secp256k1 context that CPubKey verifies with. Users of CPubKey
 *  must hold an ECCVerifyHandle. The constructor and destructor of these
 *  are not allowed to run in parallel, though. */
class ECCVerifyHandle
{
    static int32_t refcount;
    static secp256k1_context* context;

public:
    ECCVerifyHandle();
    ~ECCVerifyHandle();

    /** The verification context; null while no handle is held. */
    static const secp256k1_context* Context() { return context; }
};

#endif // TAPYRUS_ECCVERIFYHANDLE_H
