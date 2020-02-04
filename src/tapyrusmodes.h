#ifndef TAPYRUS_TAPYRUSMODES_H
#define TAPYRUS_TAPYRUSMODES_H

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <protocol.h>
#include <streams.h>


enum class TAPYRUS_OP_MODE
{
    PROD, //production 
    DEV   //development
};
namespace TAPYRUS_MODES
{

/** Tapyrus chain name strings*/
const std::string PROD = "prod";
const std::string DEV = "dev";

inline std::string GetChainName(TAPYRUS_OP_MODE mode) 
{
    switch(mode)
    {
        case TAPYRUS_OP_MODE::PROD: return PROD;
        case TAPYRUS_OP_MODE::DEV: return DEV;
        default: return PROD;
    }
}

inline int GetDefaultNetworkId(TAPYRUS_OP_MODE mode)
{
    switch(mode)
    {
        case TAPYRUS_OP_MODE::PROD: return 1;
        case TAPYRUS_OP_MODE::DEV: return 1905960821;
        default: return 1;
    }
}

}//namespace TAPYRUS_OP_MODE

#endif //TAPYRUS_TAPYRUSMODES_H