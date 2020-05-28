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
<<<<<<< HEAD
        return ColorIdentifier();
=======
        return ColorIdentifier(TokenTypes::NONE);
>>>>>>> Add token type to Coin i.e. utxo
=======
        return ColorIdentifier();
>>>>>>> Token balance verification and unit tests rebased with more fixes

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

    if(MatchCustomColoredScript(script, colorId))
        return ColorIdentifier(colorId);
<<<<<<< HEAD
    }
    return ColorIdentifier(TokenTypes::NONE);
>>>>>>> Add token type to Coin i.e. utxo
=======

    return ColorIdentifier();
>>>>>>> Token balance verification and unit tests rebased with more fixes
}