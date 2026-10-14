// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_SCRIPT_SIGNINGPROVIDER_H
#define TAPYRUS_SCRIPT_SIGNINGPROVIDER_H

#include <key.h>
#include <pubkey.h>
#include <script/script.h>
#include <script/standard.h>

#include <map>

/** An interface to be implemented by keystores that support signing. */
class SigningProvider
{
public:
    virtual ~SigningProvider() {}
    virtual bool GetCScript(const CScriptID &scriptid, CScript& script) const { return false; }
    virtual bool GetPubKey(const CKeyID &address, CPubKey& pubkey) const { return false; }
    virtual bool GetKey(const CKeyID &address, CKey& key) const { return false; }
};

extern const SigningProvider& DUMMY_SIGNING_PROVIDER;

class PublicOnlySigningProvider : public SigningProvider
{
private:
    const SigningProvider* m_provider;

public:
    PublicOnlySigningProvider(const SigningProvider* provider) : m_provider(provider) {}
    bool GetCScript(const CScriptID &scriptid, CScript& script) const override;
    bool GetPubKey(const CKeyID &address, CPubKey& pubkey) const override;
};

struct FlatSigningProvider final : public SigningProvider
{
    std::map<CScriptID, CScript> scripts;
    std::map<CKeyID, CPubKey> pubkeys;
    std::map<CKeyID, CKey> keys;

    bool GetCScript(const CScriptID& scriptid, CScript& script) const override;
    bool GetPubKey(const CKeyID& keyid, CPubKey& pubkey) const override;
    bool GetKey(const CKeyID& keyid, CKey& key) const override;
};

FlatSigningProvider Merge(const FlatSigningProvider& a, const FlatSigningProvider& b);

#endif // TAPYRUS_SCRIPT_SIGNINGPROVIDER_H
