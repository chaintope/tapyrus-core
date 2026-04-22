// Copyright (c) 2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <primitives/transaction.h>
#include <coloridentifier.h>
#include <script/script.h>
#include <script/standard.h>

ColorIdentifier GetColorIdFromScript(const CScript& script)
{
    if(!script.IsColoredScript())
        return ColorIdentifier();

    std::vector<unsigned char> pubkeyhash, colorId;
    if(script.IsColoredPayToPubkeyHash(pubkeyhash, colorId))
        return ColorIdentifier(colorId);

    if(script.IsColoredPayToScriptHash())
    {
        colorId.assign(script.begin()+1, script.begin()+34);
        return ColorIdentifier(colorId);
    }

    if(script.IsColoredBurnScript())
    {
        colorId.assign(script.begin()+1, script.begin()+34);
        return ColorIdentifier(colorId);
    }

    // Only CP2PKH, CP2SH, and burn scripts are allowed. Any other script with OP_COLOR
    // (custom scripts, decoys, stack-manipulation variants) returns NONE and
    // is rejected at transaction validation with "bad-txns-nonstandard-opcolor".
    return ColorIdentifier();
}

CKeyID CColorKeyID::getKeyID() const
{
    return CKeyID( uint160( std::vector<unsigned char>(this->begin(), this->end())));
}
