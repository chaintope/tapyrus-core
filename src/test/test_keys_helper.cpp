// Copyright (c) 2018-2019 chaintope Inc.

#include <test/test_keys_helper.h>

#include <pubkey.h>
#include <secp256k1.h>
#include <test/test_bitcoin.h>

std::vector<CPubKey> validPubKeys(unsigned int keyCount)
{
    std::vector<CPubKey> keys;
    for (unsigned int i = 0; i < keyCount; i++) {
        std::vector<unsigned char> vch = ParseHex(ValidPubKeyStrings[i]);
        CPubKey pubkey(vch.begin(), vch.end());
        keys.push_back(pubkey);
    }
    return keys;
};

std::string combinedPubkeyString(unsigned int keyCount)
{
    std::string r;
    for (unsigned int i = 0; i < keyCount; i++) {
        r += ValidPubKeyStrings[i];
    }
    return r;
};