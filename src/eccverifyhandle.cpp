// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2018-2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <eccverifyhandle.h>

#include <secp256k1.h>

#include <assert.h>

/* static */ int32_t ECCVerifyHandle::refcount = 0;
/* static */ secp256k1_context* ECCVerifyHandle::context = nullptr;

ECCVerifyHandle::ECCVerifyHandle()
{
    if (refcount == 0) {
        assert(context == nullptr);
        context = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
        assert(context != nullptr);
    }
    refcount++;
}

ECCVerifyHandle::~ECCVerifyHandle()
{
    refcount--;
    if (refcount == 0) {
        assert(context != nullptr);
        secp256k1_context_destroy(context);
        context = nullptr;
    }
}
