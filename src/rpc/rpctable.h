// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_RPC_RPCTABLE_H
#define TAPYRUS_RPC_RPCTABLE_H

#include <rpc/jsonrpcrequest.h>

#include <univalue.h>

#include <map>
#include <string>
#include <vector>

typedef UniValue(*rpcfn_type)(const JSONRPCRequest& jsonRequest);

class CRPCCommand
{
public:
    std::string category;
    std::string name;
    rpcfn_type actor;
    std::vector<std::string> argNames;
};

/**
 * Bitcoin RPC command dispatcher.
 */
class CRPCTable
{
private:
    std::map<std::string, const CRPCCommand*> mapCommands;
public:
    CRPCTable();
    const CRPCCommand* operator[](const std::string& name) const;
    std::string help(const std::string& name, const JSONRPCRequest& helpreq) const;

    /**
     * Execute a method.
     * @param request The JSONRPCRequest to execute
     * @returns Result of the call.
     * @throws an exception (UniValue) when an error happens.
     */
    UniValue execute(const JSONRPCRequest &request) const;

    /**
    * Returns a list of registered commands
    * @returns List of registered commands.
    */
    std::vector<std::string> listCommands() const;


    /**
     * Appends a CRPCCommand to the dispatch table.
     *
     * Returns false if RPC server is already running (dump concurrency protection).
     *
     * Commands cannot be overwritten (returns false).
     *
     * Commands with different method names but the same callback function will
     * be considered aliases, and only the first registered method name will
     * show up in the help text command listing. Aliased commands do not have
     * to have the same behavior. Server and client code can distinguish
     * between calls based on method name, and aliased commands can also
     * register different names, types, and numbers of parameters.
     */
    bool appendCommand(const std::string& name, const CRPCCommand* pcmd);
};

extern CRPCTable tableRPC;

#endif // TAPYRUS_RPC_RPCTABLE_H
