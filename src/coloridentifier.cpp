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
<<<<<<< HEAD
        return ColorIdentifier();
=======
        return ColorIdentifier(TokenTypes::NONE);
>>>>>>> Add token type to Coin i.e. utxo

    std::vector<unsigned char> pubkeyhash, colorId;
    if( MatchColoredPayToPubkeyHash(script, pubkeyhash, colorId))
        return ColorIdentifier(colorId);

    if(script.IsColoredPayToScriptHash())
    {
<<<<<<< HEAD
        colorId.assign(script.begin()+1, script.begin()+34);
        return ColorIdentifier(colorId);
    }

    if(MatchCustomColoredScript(script, colorId))
        return ColorIdentifier(colorId);

    return ColorIdentifier();
=======
        TokenTypes type = UintToToken(*(script.begin() + 1));

        if(type == TokenTypes::REISSUABLE)
            colorId.assign(script.begin()+1, script.begin()+34);
        else
            colorId.assign(script.begin()+1, script.begin()+38);
        return ColorIdentifier(colorId);
    }

    //search for colorid in the script
    CScript::const_iterator iter = std::find(script.begin(), script.end(), OP_COLOR);
    if(iter != script.end())
    {
        size_t index = std::distance(script.begin(), iter);
        if(index <= 33) 
            return ColorIdentifier(TokenTypes::NONE);

        if(index > 33 && script[index - 33] == 0x21) 
            colorId.assign(script.begin()+index-32, script.begin()+index);
        else if(index > 37 && script[index - 37] == 0x25) 
            colorId.assign(script.begin()+index-36, script.begin()+index);

        return ColorIdentifier(colorId);
    }
    return ColorIdentifier(TokenTypes::NONE);
>>>>>>> Add token type to Coin i.e. utxo
}