// Copyright (c) 2014-2017 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>

#include <base58.h>
#include <bech32.h>
#include <script/script.h>
#include <utilstrencodings.h>

#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>

#include <assert.h>
#include <string.h>
#include <algorithm>
#include <coloridentifier.h>

namespace
{
class DestinationEncoder : public boost::static_visitor<std::string>
{
private:
    const CChainParams& m_params;
    ColorIdentifier& colorId;
public:
    explicit DestinationEncoder(const CChainParams& params, ColorIdentifier& colorIdin) : m_params(params), colorId(colorIdin) { }

    std::string operator()(const CKeyID& id) const
    {
        std::vector<unsigned char> data;
        //<COLOR identifier> OP_COLOR OP_DUP OP_HASH160 <H(pubkey)> OP_EQUALVERIFY OP_CHECKSIG
        if (colorId.type != TokenTypes::NONE) {
            std::vector<unsigned char> cid = colorId.toVector();
            // data = m_params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
            data.push_back(0x21);
            data.insert(data.end(), cid.begin(), cid.end());
            data.push_back(OP_COLOR);
            data.push_back(OP_DUP);
            data.push_back(OP_HASH160);
            data.push_back(0x14);
            data.insert(data.end(), id.begin(), id.end());
            data.push_back(OP_EQUALVERIFY);
            data.push_back(OP_CHECKSIG);
        } else {
            data = m_params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
            data.insert(data.end(), id.begin(), id.end());
        }
        return EncodeBase58Check(data);
    }

    std::string operator()(const CScriptID& id) const
    {
        // <COLOR identifier> OP_COLOR OP_HASH160 <H(redeem script)> OP_EQUAL
        std::vector<unsigned char> data;
        if (colorId.type != TokenTypes::NONE) {
        } else {
            data = m_params.Base58Prefix(CChainParams::SCRIPT_ADDRESS);
            data.insert(data.end(), id.begin(), id.end());
        }
        return EncodeBase58Check(data);
    }
#ifdef DEBUG
    std::string operator()(const WitnessV0KeyHash& id) const
    {
        std::vector<unsigned char> data = {0};
        data.insert(data.end(), id.begin(), id.end());
        return EncodeBase58Check(data); //invalid address
    }

    std::string operator()(const WitnessV0ScriptHash& id) const
    {
        std::vector<unsigned char> data = {0};
        data.insert(data.end(), id.begin(), id.end());
        return EncodeBase58Check(data); //invalid address
    }

    std::string operator()(const WitnessUnknown& id) const
    {
        if (id.version < 1 || id.version > 16 || id.length < 2 || id.length > 40) {
            return {};
        }
        std::vector<unsigned char> data = {(unsigned char)id.version};
        data.insert(data.end(), id.program, id.program + id.length);
        return EncodeBase58Check(data); //invalid address
    }
#endif
    std::string operator()(const CNoDestination& no) const { return {}; }
};

CTxDestination DecodeDestination(const std::string& str, const CChainParams& params, ColorIdentifier& colorId)
{
    std::vector<unsigned char> data;
    std::vector<unsigned char> temp;
    uint160 hash;
    if (DecodeBase58Check(str, data)) {

        CScript scriptPubKey(data.begin(), data.end());
        if(scriptPubKey.IsColoredScript()) {
            ColorIdentifier cid(GetColorIdFromScript(scriptPubKey));
            colorId = cid;
            std::vector<unsigned char> pkh;
            std::vector<unsigned char> c1;
            if (MatchColoredPayToPubkeyHash(scriptPubKey, pkh, c1)) {
                temp.push_back(0x00);
                temp.insert(temp.end(), data.begin()+38, data.begin()+58);
                data.clear();
                data = temp;
            }
        }

        // base58-encoded Tapyrus addresses.
        // Public-key-hash-addresses have version 0 (or 111 testnet).
        // The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
        const std::vector<unsigned char>& pubkey_prefix = params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
        if (data.size() == hash.size() + pubkey_prefix.size() && std::equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin())) {
            std::copy(data.begin() + pubkey_prefix.size(), data.end(), hash.begin());
            return CKeyID(hash);
        }
        // Script-hash-addresses have version 5 (or 196 testnet).
        // The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
        const std::vector<unsigned char>& script_prefix = params.Base58Prefix(CChainParams::SCRIPT_ADDRESS);
        if (data.size() == hash.size() + script_prefix.size() && std::equal(script_prefix.begin(), script_prefix.end(), data.begin())) {
            std::copy(data.begin() + script_prefix.size(), data.end(), hash.begin());
            return CScriptID(hash);
        }
    }
    data.clear();
    return CNoDestination();
}
} // namespace

CKey DecodeSecret(const std::string& str)
{
    CKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data)) {
        const std::vector<unsigned char>& privkey_prefix = Params().Base58Prefix(CChainParams::SECRET_KEY);
        if ((data.size() == 32 + privkey_prefix.size() || (data.size() == 33 + privkey_prefix.size() && data.back() == 1)) &&
            std::equal(privkey_prefix.begin(), privkey_prefix.end(), data.begin())) {
            bool compressed = data.size() == 33 + privkey_prefix.size();
            key.Set(data.begin() + privkey_prefix.size(), data.begin() + privkey_prefix.size() + 32, compressed);
        }
    }
    memory_cleanse(data.data(), data.size());
    return key;
}

std::string EncodeSecret(const CKey& key)
{
    assert(key.IsValid());
    std::vector<unsigned char> data = Params().Base58Prefix(CChainParams::SECRET_KEY);
    data.insert(data.end(), key.begin(), key.end());
    if (key.IsCompressed()) {
        data.push_back(1);
    }
    std::string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

CExtPubKey DecodeExtPubKey(const std::string& str)
{
    CExtPubKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data)) {
        const std::vector<unsigned char>& prefix = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
            key.Decode(data.data() + prefix.size());
        }
    }
    return key;
}

std::string EncodeExtPubKey(const CExtPubKey& key)
{
    std::vector<unsigned char> data = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
    size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    return ret;
}

CExtKey DecodeExtKey(const std::string& str)
{
    CExtKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data)) {
        const std::vector<unsigned char>& prefix = Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
            key.Decode(data.data() + prefix.size());
        }
    }
    return key;
}

std::string EncodeExtKey(const CExtKey& key)
{
    std::vector<unsigned char> data = Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
    size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

std::string EncodeDestination(const CTxDestination& dest, ColorIdentifier& colorId)
{
    return boost::apply_visitor(DestinationEncoder(Params(), colorId), dest);
}

CTxDestination DecodeDestination(const std::string& str, ColorIdentifier& colorId)
{
    return DecodeDestination(str, Params(), colorId);
}

bool IsValidDestinationString(const std::string& str, const CChainParams& params)
{
    ColorIdentifier colorId;
    return IsValidDestination(DecodeDestination(str, params, colorId));
}

bool IsValidDestinationString(const std::string& str)
{
    return IsValidDestinationString(str, Params());
}
