// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_SCRIPT_BASESIGNATURECHECKER_H
#define TAPYRUS_SCRIPT_BASESIGNATURECHECKER_H

#include <vector>

class CPubKey;
class CScript;
class CScriptNum;
class uint256;

class BaseSignatureChecker
{
public:
    virtual bool VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& vchPubKey, const uint256& sighash) const
    {
        return false;
    }

    virtual bool CheckSig(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode) const
    {
        return false;
    }

    virtual bool CheckLockTime(const CScriptNum& nLockTime) const
    {
         return false;
    }

    virtual bool CheckSequence(const CScriptNum& nSequence) const
    {
         return false;
    }

    virtual ~BaseSignatureChecker() {}
};

#endif // TAPYRUS_SCRIPT_BASESIGNATURECHECKER_H
