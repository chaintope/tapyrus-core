// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_RPC_JSONRPCREQUEST_H
#define TAPYRUS_RPC_JSONRPCREQUEST_H

#include <univalue.h>

#include <string>

struct NodeContext;

class JSONRPCRequest
{
public:
    UniValue id;
    std::string strMethod;
    UniValue params;
    bool fHelp;
    std::string URI;
    std::string authUser;
    std::string peerAddr;
    //! The node the request runs against.
    NodeContext& context;

    explicit JSONRPCRequest(NodeContext& node) : id(NullUniValue), params(NullUniValue), fHelp(false), context(node) {}
    void parse(const UniValue& valRequest);
};

#endif // TAPYRUS_RPC_JSONRPCREQUEST_H
