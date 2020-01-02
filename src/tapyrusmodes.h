#ifndef TAPYRUS_MODES_H
#define TAPYRUS_MODES_H

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <protocol.h>
#include <streams.h>


enum class TAPYRUS_OP_MODE
{
    MAIN,
    REGTEST
};
namespace TAPYRUS_MODES
{

/** BIP70 chain name strings (main or regtest) */
const std::string MAIN = "main";
const std::string REGTEST = "regtest";

inline std::string GetChainName(TAPYRUS_OP_MODE mode) 
{
    switch(mode)
    {
        case TAPYRUS_OP_MODE::MAIN: return MAIN;
        case TAPYRUS_OP_MODE::REGTEST: return REGTEST;
        default: return MAIN;
    }
}

inline int GetDefaultNetworkId(TAPYRUS_OP_MODE mode)
{
    switch(mode)
    {
        case TAPYRUS_OP_MODE::MAIN: return 1;
        case TAPYRUS_OP_MODE::REGTEST: return 1905960821;
        default: return 1;
    }
}

}//namespace TAPYRUS_OP_MODE

#endif //TAPYRUS_MODES_H