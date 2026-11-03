// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <genesisfile.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <fstream>
#include <stdexcept>

std::vector<unsigned char> ReadGenesisBlock(fs::path genesisPath)
{
    std::string genesisFileName(TAPYRUS_GENESIS_FILENAME);

    //if network id was passed read genesis.<networkid>
    if(gArgs.IsArgSet("-networkid"))
        genesisFileName.replace(8, 3, std::to_string(gArgs.GetArg("-networkid", 0)));
    genesisPath /= genesisFileName;

    LogPrintf("Reading Genesis Block from [%s]\n", genesisPath.string().c_str());
    std::ifstream stream(genesisPath.string());
    if (!stream.good())
        throw std::runtime_error(strprintf("ReadGenesisBlock: unable to read genesis file %s", genesisPath));

    std::string genesisHex;
    stream >> genesisHex;
    stream.close();

    std::vector<unsigned char> genesis = ParseHex(genesisHex);
    if (genesisHex.length() != genesis.size() * 2)
        throw std::runtime_error("ReadGenesisBlock: invalid genesis file");
    return genesis;
}
