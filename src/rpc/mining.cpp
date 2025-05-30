// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <validation.h>
#include <key_io.h>
#include <miner.h>
#include <net.h>
#include <policy/fees.h>
#include <rpc/blockchain.h>
#include <rpc/mining.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <shutdown.h>
#include <txmempool.h>
#include <util.h>
#include <utilstrencodings.h>
#include <validationinterface.h>
#include <warnings.h>
#include <xfieldhistory.h>

#include <memory>
#include <chrono>

#include <stdint.h>
#include <policy/policy.h>

unsigned int ParseConfirmTarget(const UniValue& value)
{
    int target = value.get_int();
    unsigned int max_target = ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    if (target < 1 || (unsigned int)target > max_target) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid conf_target, must be between %u - %u", 1, max_target));
    }
    return (unsigned int)target;
}

UniValue generateBlocks(std::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, bool keepScript, const CKey& privKey)
{
    uint32_t nHeightEnd = 0;
    uint32_t nHeight = 0;

    {   // Don't keep cs_main locked
        LOCK(cs_main);
        nHeight = chainActive.Height();
        nHeightEnd = nHeight+nGenerate;
    }
    unsigned int nExtraNonce = 0;
    UniValue blockHashes(UniValue::VARR);
    while (nHeight < nHeightEnd && !ShutdownRequested())
    {
        std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript));
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");
        CBlock *pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
        }

        std::vector<unsigned char> proof;
        uint256 blockHash = pblock->GetHashForSign();

        privKey.Sign_Schnorr(blockHash, proof);

        XFieldAggPubKey aggpubkeyChange;
        CXFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);

        if(!pblock->AbsorbBlockProof(proof, aggpubkeyChange.getPubKey())) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "AbsorbBlockProof, block proof not accepted");
        }

        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        if (!ProcessNewBlock(shared_pblock, true, nullptr))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        //mark script as important because it was used at least for one coinbase output if the script came from the wallet
        if (keepScript)
        {
            coinbaseScript->KeepScript();
        }
    }
    return blockHashes;
}

static UniValue generatetoaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "generatetoaddress nblocks address ()\n"
            "\nMine blocks immediately to a specified address (before the RPC call returns)\n"
            "\nArguments:\n"
            "1. nblocks      (numeric, required) How many blocks are generated immediately.\n"
            "2. address      (string, required) The address to send the newly generated bitcoin to.\n"
            "3. private key  (string, required) The aggregate private key to sign the generated blocks. The format is WIF.\n"
            "\nResult:\n"
            "[ blockhashes ]     (array) hashes of blocks generated\n"
            "\nExamples:\n"
            "\nGenerate 11 blocks to myaddress signed with 1 private key"
            + HelpExampleCli("generatetoaddress", "11 \"myaddress\" \"L3wNcWW97SZYV6fb6b5VgoEvtybc9d1Uu6LeiQkVN4Z38VTivKT6\"")
        );

    int nGenerate = request.params[0].get_int();

    CTxDestination destination = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    std::shared_ptr<CReserveScript> coinbaseScript = std::make_shared<CReserveScript>();
    coinbaseScript->reserveScript = GetScriptForDestination(destination);

    CKey cPrivKey = DecodeSecret(request.params[2].get_str());
    if(!cPrivKey.IsValid())
        throw JSONRPCError(RPC_WALLET_INVALID_PRIVATE_KEY, "No private key given or invalid private key.");

    XFieldAggPubKey aggpubkeyChange;
    CXFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);

    if(cPrivKey.GetPubKey() != aggpubkeyChange.getPubKey())
        throw JSONRPCError(RPC_WALLET_INVALID_AGGREGATE_KEY, "Given private key doesn't correspond to the Aggregate Key.");

    return generateBlocks(coinbaseScript, nGenerate, false, cPrivKey);
}

UniValue getnewblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
                "getnewblock\n"
                "\nGets hex representation of a proposed, unmined new block\n"
                "\nArguments:\n"
                "1. address                    (string, required) The address to send fees and the newly generated coin.\n"
                "2. required_age               (numeric, optional, default=0) How many seconds a transaction must have been in the mempool to be inluded in the block proposal. This may help with faster block convergence among functionaries using compact blocks.\n"
                "3. xfield_type:new_xfield_value (multiple, optional) Change network wide value of the xfield parameter from the next block. The xfield parameter is identified by the xfield_type.\n"
                " xfield_type supported are:\n"
                "    1 : Aggregate Public Key of the Tapyrus Signer network.  (string )\n"
                "\nResult\n"
                "blockhex      (hex) The block hex\n"
                "\nExamples:\n"
                + HelpExampleCli("getnewblock", "")
                + HelpExampleCli("getnewblock", "\"mt8EZJFAhhhxv57NFYfXPecDoAbWWqnRqX\" 10 \"1:03831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc\"")
        );

    CTxDestination destination = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    int required_wait = !request.params[1].isNull() ? request.params[1].get_int() : 0;
    if (required_wait < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMS, "required_wait must be non-negative.");
    }

    const std::string xfieldParam = request.params[2].isNull() ? "" : request.params[2].get_str();
    CXField xfield;
    if (!request.params[2].isNull())
    {
        if(xfieldParam.find(':') == std::string::npos)
            throw JSONRPCError(RPC_INVALID_PARAMS, "xfield parameter could not be parsed. Check if the xfield parameter has format: <xfield_type:new_xfield_value>.");

        //using lambda to avoid temp variables
        xfield.xfieldType = [xfieldParam](int splitAt, TAPYRUS_XFIELDTYPES max) -> TAPYRUS_XFIELDTYPES
            { int x = splitAt > 0 ? atoi(xfieldParam.substr(0,splitAt)) : 0;
            return x > 0 && x <= int(max) ? TAPYRUS_XFIELDTYPES(x) : TAPYRUS_XFIELDTYPES::NONE;
            } (xfieldParam.find(':'), TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE );

        if(xfield.xfieldType ==  TAPYRUS_XFIELDTYPES::NONE)
            throw JSONRPCError(RPC_INVALID_PARAMS, "Unknown xfield type");

        switch(xfield.xfieldType){
            case TAPYRUS_XFIELDTYPES::AGGPUBKEY:
            {
                std::string aggPubkeyString = xfieldParam.substr(xfieldParam.find(':')+1);
                if (IsHex(aggPubkeyString)) {
                    std::vector<unsigned char> data = ParseHex(aggPubkeyString);
                    CPubKey aggPubKey(data);
                    if (aggPubKey.IsFullyValid() && aggPubKey.IsCompressed())
                    {
                        xfield.xfieldValue = XFieldAggPubKey(data);
                        xfield.xfieldType = TAPYRUS_XFIELDTYPES::AGGPUBKEY;
                        break;
                    }
                    throw JSONRPCError(RPC_INVALID_PARAMS, "xfield parameter was invalid. Aggregate public key was uncompressed or invalid");
                }
                throw JSONRPCError(RPC_INVALID_PARAMS, "xfield parameter was invalid. Aggregate public key could not be parsed");
            }
            break;
            case TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE:
            {
                std::string xfieldString = xfieldParam.substr(xfieldParam.find(':')+1);
                uint32_t val;
                if(!xfieldString.size() || !ParseUInt32(xfieldString, &val))
                    throw JSONRPCError(RPC_INVALID_PARAMS, "xfield max block size was invalid. It is expected to be <xfield_type:new_xfield_value>.");
                xfield.xfieldValue = XFieldMaxBlockSize(val);
                if(!std::visit(XFieldValidityVisitor(), xfield.xfieldValue))
                    throw JSONRPCError(RPC_INVALID_PARAMS, "xfield max block size was invalid. It is expected to be a positive quantity between 1000 and 4294967295.");
                xfield.xfieldType = TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE;
            }
            break;
            default:
                throw JSONRPCError(RPC_INVALID_PARAMS, "xfield parameter was invalid. Unknown xfield type");
        }
    }
    CScript coinbaseScript {GetScriptForDestination(destination)};

    std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbaseScript, true, &xfield));
    if (!pblocktemplate.get())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Wallet keypool empty");
    {
        // IncrementExtraNonce sets coinbase flags and builds merkle tree
        LOCK(cs_main);
        unsigned int nExtraNonce = 0;
        IncrementExtraNonce(&pblocktemplate->block, chainActive.Tip(), nExtraNonce);
    }

    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << pblocktemplate->block;
    return HexStr(ssBlock.begin(), ssBlock.end());
}

static UniValue getmininginfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmininginfo\n"
            "\nReturns a json object containing mining-related information."
            "\nResult:\n"
            "{\n"
            "  \"blocks\": nnn,             (numeric) The current block\n"
            "  \"currentblocksize\": nnn,   (numeric) The last block size\n"
            "  \"currentblocktx\": nnn,     (numeric) The last block transaction\n"
            "  \"pooledtx\": n              (numeric) The size of the mempool\n"
            "  \"chain\": \"xxxx\",           (string) current network name\n"
            "  \"warnings\": \"...\"          (string) any network and blockchain warnings\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmininginfo", "")
            + HelpExampleRpc("getmininginfo", "")
        );


    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks",           (int)chainActive.Height());
    obj.pushKV("currentblocksize", (uint64_t)nLastBlockSize);
    obj.pushKV("currentblocktx",   (uint64_t)nLastBlockTx);
    // TODO: push signed block multisig condition
    obj.pushKV("pooledtx",         (uint64_t)mempool.size());
    obj.pushKV("chain",            FederationParams().NetworkIDString());
    obj.pushKV("warnings",         GetWarnings("statusbar"));
    return obj;
}



// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, FormatStateMessage(state));
    if (state.IsInvalid())
    {
        std::string strRejectReason = state.GetRejectReason();
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}


static UniValue getblocktemplate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getblocktemplate ( TemplateRequest )\n"
            "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
            "It returns data needed to construct a block to work on.\n"
            "For full specification, see BIPs 22, 23, 9, and 145:\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0023.mediawiki\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0009.mediawiki#getblocktemplate_changes\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0145.mediawiki\n"

            "\nArguments:\n"
            "1. template_request         (json object, optional) A json object in the following spec\n"
            "     {\n"
            "       \"mode\":\"template\"    (string, optional) This must be set to \"template\", \"proposal\" (see BIP 23), or omitted\n"
            "       \"capabilities\":[     (array, optional) A list of strings\n"
            "           \"support\"          (string) client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'\n"
            "           ,...\n"
            "       ],\n"
            "       \"rules\":[            (array, optional) A list of strings\n"
            "           \"support\"          (string) client side supported softfork deployment\n"
            "           ,...\n"
            "       ]\n"
            "     }\n"
            "\n"

            "\nResult:\n"
            "{\n"
            "  \"features\" : n,                    (numeric) The preferred block features\n"
            "  \"rules\" : [ \"rulename\", ... ],    (array of strings) specific block rules that are to be enforced\n"
            "      \"rulename\" : bitnumber          (numeric) identifies the bit number as indicating acceptance and readiness for the named softfork rule\n"
            "      ,...\n"
            "  },\n"
            "  \"previousblockhash\" : \"xxxx\",     (string) The hash of current highest block\n"
            "  \"transactions\" : [                (array) contents of non-coinbase transactions that should be included in the next block\n"
            "      {\n"
            "         \"data\" : \"xxxx\",             (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
            "         \"txid\" : \"xxxx\",             (string) transaction id encoded in little-endian hexadecimal\n"
            "         \"hash\" : \"xxxx\",             (string) hash encoded in little-endian hexadecimal\n"
            "         \"depends\" : [                (array) array of numbers \n"
            "             n                          (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is\n"
            "             ,...\n"
            "         ],\n"
            "         \"fee\": n,                    (numeric) difference in value between transaction inputs and outputs (in tapyrus); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one\n"
            "         \"sigops\" : n,                (numeric) total SigOps cost, as counted for purposes of block limits; if key is not present, sigop cost is unknown and clients MUST NOT assume it is zero\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
            "  \"coinbaseaux\" : {                 (json object) data that should be included in the coinbase's scriptSig content\n"
            "      \"flags\" : \"xx\"                  (string) key name is to be ignored, and value included in scriptSig\n"
            "  },\n"
            "  \"coinbasevalue\" : n,              (numeric) maximum allowable input to coinbase transaction, including the generation award and transaction fees (in tapyrus)\n"
            "  \"coinbasetxn\" : { ... },          (json object) information for coinbase transaction\n"
            "  \"target\" : \"xxxx\",                (string) The hash target\n"
            "  \"mintime\" : xxx,                  (numeric) The minimum timestamp appropriate for next block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mutable\" : [                     (array of string) list of ways the block template may be changed \n"
            "     \"value\"                          (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'\n"
            "     ,...\n"
            "  ],\n"
            "  \"noncerange\" : \"00000000ffffffff\",(string) A range of valid nonces\n"
            "  \"sigoplimit\" : n,                 (numeric) limit of sigops in blocks\n"
            "  \"sizelimit\" : n,                  (numeric) limit of block size\n"
            "  \"curtime\" : ttt,                  (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"bits\" : \"xxxxxxxx\",              (string) compressed target of next block\n"
            "  \"height\" : n                      (numeric) The height of the next block\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getblocktemplate", "")
            + HelpExampleRpc("getblocktemplate", "")
         );

    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    std::set<std::string> setClientRules;
    if (!request.params[0].isNull())
    {
        const UniValue& oparam = request.params[0].get_obj();
        const UniValue& modeval = oparam.find_value("mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull())
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = oparam.find_value("longpollid");

        if (strMode == "proposal")
        {
            const UniValue& dataval = oparam.find_value("data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            const CBlockIndex* pindex = LookupBlockIndex(hash);
            if (pindex) {
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = chainActive.Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";
            CValidationState state;
            TestBlockValidity(state, block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }

        const UniValue& aClientRules = oparam.find_value("rules");
        if (aClientRules.isArray()) {
            for (unsigned int i = 0; i < aClientRules.size(); ++i) {
                const UniValue& v = aClientRules[i];
                setClientRules.insert(v.get_str());
            }
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if(!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Tapyrus is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Tapyrus is downloading blocks...");

    static unsigned int nTransactionsUpdatedLast;

    if (!lpval.isNull())
    {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        std::chrono::steady_clock::time_point checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr())
        {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain.SetHex(lpstr.substr(0, 64));
            nTransactionsUpdatedLastLP = atoi64(lpstr.substr(64));
        }
        else
        {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = chainActive.Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release the wallet and main lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = std::chrono::steady_clock::now() + std::chrono::minutes(1);

            WaitableLock lock(g_best_block_mutex);
            while (g_best_block == hashWatchedChain && IsRPCRunning())
            {
                if (g_best_block_cv.wait_until(lock, checktxtime) == std::cv_status::timeout)
                {
                    // Timeout: Check transactions for update
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += std::chrono::seconds(10);
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    // If the caller is indicating segwit support, then allow CreateNewBlock()
    // to select witness transactions, after segwit activates (otherwise
    // don't).
    bool fSupportsSegwit = false;

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static std::unique_ptr<CBlockTemplate> pblocktemplate;
    // Cache whether the last invocation was with segwit support, to avoid returning
    // a segwit-block to a non-segwit caller.
    static bool fLastTemplateSupportsSegwit = false;
    if (pindexPrev != chainActive.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5) ||
        fLastTemplateSupportsSegwit != fSupportsSegwit)
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = chainActive.Tip();
        nStart = GetTime();
        fLastTemplateSupportsSegwit = fSupportsSegwit;

        // Create new block
        CScript scriptDummy = CScript() << OP_TRUE;
        pblocktemplate = BlockAssembler(Params()).CreateNewBlock(scriptDummy, fSupportsSegwit);
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }
    assert(pindexPrev);
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience
    const Consensus::Params& consensusParams = Params().GetConsensus();

    // Update nTime
    UpdateTime(pblock, consensusParams, pindexPrev);

    UniValue aCaps(UniValue::VARR); aCaps.push_back("proposal");

    UniValue transactions(UniValue::VARR);
    std::map<uint256, int64_t> setTxIndex;
    int i = 0;
    for (const auto& it : pblock->vtx) {
        const CTransaction& tx = *it;
        uint256 txHash = tx.GetHashMalFix();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));
        entry.pushKV("txid", txHash.GetHex());
        entry.pushKV("hash", tx.GetWitnessHash().GetHex());

        UniValue deps(UniValue::VARR);
        for (const CTxIn &in : tx.vin)
        {
            if (setTxIndex.count(in.prevout.hashMalFix))
                deps.push_back(setTxIndex[in.prevout.hashMalFix]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = i - 1;
        entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
        int32_t nTxSigOps = pblocktemplate->vTxSigOpsCost[index_in_template];
        entry.pushKV("sigops", nTxSigOps);
        entry.pushKV("size", (int64_t)GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));

        transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);
    aux.pushKV("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end()));

    UniValue aMutable(UniValue::VARR);
    aMutable.push_back("time");
    aMutable.push_back("transactions");
    aMutable.push_back("prevblock");

    UniValue result(UniValue::VOBJ);
    result.pushKV("capabilities", aCaps);

    UniValue aRules(UniValue::VARR);
    result.pushKV("features", pblock->nFeatures);
    result.pushKV("rules", aRules);

    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("transactions", transactions);
    result.pushKV("coinbaseaux", aux);
    result.pushKV("coinbasevalue", (int64_t)pblock->vtx[0]->vout[0].nValue);
    result.pushKV("longpollid", chainActive.Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast));
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast()+1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");
    result.pushKV("sigoplimit", (int64_t)GetMaxBlockSigops());
    result.pushKV("sizelimit", (int64_t)GetCurrentMaxBlockSize());
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("proof", HexStr(pblock->proof));
    result.pushKV("height", (pindexPrev->nHeight+1));

    if (!pblocktemplate->vchCoinbaseCommitment.empty() && fSupportsSegwit) {
        result.pushKV("default_witness_commitment", HexStr(pblocktemplate->vchCoinbaseCommitment.begin(), pblocktemplate->vchCoinbaseCommitment.end()));
    }

    return result;
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    explicit submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state() {}

protected:
    void BlockChecked(const CBlock& block, const CValidationState& stateIn) override {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    }
};

static UniValue submitblock(const JSONRPCRequest& request)
{
    // We allow 2 arguments for compliance with BIP22. Argument 2 is ignored.
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "submitblock \"hexdata\"  ( \"dummy\" )\n"
            "\nAttempts to submit new block to network.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments\n"
            "1. \"hexdata\"        (string, required) the hex-encoded block data to submit\n"
            "2. \"dummy\"          (optional) dummy value, for compatibility with BIP22. This value is ignored.\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("submitblock", "\"mydata\"")
            + HelpExampleRpc("submitblock", "\"mydata\"")
        );
    }

    std::shared_ptr<CBlock> blockptr = std::make_shared<CBlock>();
    CBlock& block = *blockptr;
    if (!DecodeHexBlk(block, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase");
    }

    uint256 hash = block.GetHash();
    {
        LOCK(cs_main);
        const CBlockIndex* pindex = LookupBlockIndex(hash);
        if (pindex) {
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
                return "duplicate";
            }
            if (pindex->nStatus & BLOCK_FAILED_MASK) {
                return "duplicate-invalid";
            }
        }
    }

    bool new_block;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool accepted = ProcessNewBlock(blockptr, /* fForceProcessing */ true, /* fNewBlock */ &new_block);
    UnregisterValidationInterface(&sc);
    if (!new_block) {
        if (!accepted) {
            // TODO Maybe pass down fNewBlock to AcceptBlockHeader, so it is properly set to true in this case?
            return "invalid";
        }
        return "duplicate";
    }
    if (!sc.found) {
        return "inconclusive";
    }
    return BIP22ValidationResult(sc.state);
}

static UniValue estimatefee(const JSONRPCRequest& request)
{
    throw JSONRPCError(RPC_METHOD_DEPRECATED, "estimatefee was removed in v0.17.\n"
        "Clients should use estimatesmartfee.");
}

static UniValue estimatesmartfee(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "estimatesmartfee conf_target (\"estimate_mode\")\n"
            "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
            "confirmation within conf_target blocks if possible and return the number of blocks\n"
            "for which the estimate is valid.\n"
            "\nArguments:\n"
            "1. conf_target     (numeric) Confirmation target in blocks (1 - 1008)\n"
            "2. \"estimate_mode\" (string, optional, default=CONSERVATIVE) The fee estimate mode.\n"
            "                   Whether to return a more conservative estimate which also satisfies\n"
            "                   a longer history. A conservative estimate potentially returns a\n"
            "                   higher feerate and is more likely to be sufficient for the desired\n"
            "                   target, but is not as responsive to short term drops in the\n"
            "                   prevailing fee market.  Must be one of:\n"
            "       \"UNSET\" (defaults to CONSERVATIVE)\n"
            "       \"ECONOMICAL\"\n"
            "       \"CONSERVATIVE\"\n"
            "\nResult:\n"
            "{\n"
            "  \"feerate\" : x.x,     (numeric, optional) estimate fee rate in " + CURRENCY_UNIT + "/kB\n"
            "  \"errors\": [ str... ] (json array of strings, optional) Errors encountered during processing\n"
            "  \"blocks\" : n         (numeric) block number where estimate was found\n"
            "}\n"
            "\n"
            "The request target will be clamped between 2 and the highest target\n"
            "fee estimation is able to return based on how long it has been running.\n"
            "An error is returned if not enough transactions and blocks\n"
            "have been observed to make an estimate for any number of blocks.\n"
            "\nExample:\n"
            + HelpExampleCli("estimatesmartfee", "6")
            );

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VSTR});
    RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
    unsigned int conf_target = ParseConfirmTarget(request.params[0]);
    bool conservative = true;
    if (!request.params[1].isNull()) {
        FeeEstimateMode fee_mode;
        if (!FeeModeFromString(request.params[1].get_str(), fee_mode)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
        if (fee_mode == FeeEstimateMode::ECONOMICAL) conservative = false;
    }

    UniValue result(UniValue::VOBJ);
    UniValue errors(UniValue::VARR);
    FeeCalculation feeCalc;
    CFeeRate feeRate = ::feeEstimator.estimateSmartFee(conf_target, &feeCalc, conservative);
    if (feeRate != CFeeRate(0)) {
        result.pushKV("feerate", ValueFromAmount(feeRate.GetFeePerK()));
    } else {
        errors.push_back("Insufficient data or no feerate found");
        result.pushKV("errors", errors);
    }
    result.pushKV("blocks", feeCalc.returnedTarget);
    return result;
}

static UniValue estimaterawfee(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "estimaterawfee conf_target (threshold)\n"
            "\nWARNING: This interface is unstable and may disappear or change!\n"
            "\nWARNING: This is an advanced API call that is tightly coupled to the specific\n"
            "         implementation of fee estimation. The parameters it can be called with\n"
            "         and the results it returns will change if the internal implementation changes.\n"
            "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
            "confirmation within conf_target blocks if possible.\n"
            "\nArguments:\n"
            "1. conf_target (numeric) Confirmation target in blocks (1 - 1008)\n"
            "2. threshold   (numeric, optional) The proportion of transactions in a given feerate range that must have been\n"
            "               confirmed within conf_target in order to consider those feerates as high enough and proceed to check\n"
            "               lower buckets.  Default: 0.95\n"
            "\nResult:\n"
            "{\n"
            "  \"short\" : {            (json object, optional) estimate for short time horizon\n"
            "      \"feerate\" : x.x,        (numeric, optional) estimate fee rate in " + CURRENCY_UNIT + "/kB\n"
            "      \"decay\" : x.x,          (numeric) exponential decay (per block) for historical moving average of confirmation data\n"
            "      \"scale\" : x,            (numeric) The resolution of confirmation targets at this time horizon\n"
            "      \"pass\" : {              (json object, optional) information about the lowest range of feerates to succeed in meeting the threshold\n"
            "          \"startrange\" : x.x,     (numeric) start of feerate range\n"
            "          \"endrange\" : x.x,       (numeric) end of feerate range\n"
            "          \"withintarget\" : x.x,   (numeric) number of txs over history horizon in the feerate range that were confirmed within target\n"
            "          \"totalconfirmed\" : x.x, (numeric) number of txs over history horizon in the feerate range that were confirmed at any point\n"
            "          \"inmempool\" : x.x,      (numeric) current number of txs in mempool in the feerate range unconfirmed for at least target blocks\n"
            "          \"leftmempool\" : x.x,    (numeric) number of txs over history horizon in the feerate range that left mempool unconfirmed after target\n"
            "      },\n"
            "      \"fail\" : { ... },       (json object, optional) information about the highest range of feerates to fail to meet the threshold\n"
            "      \"errors\":  [ str... ]   (json array of strings, optional) Errors encountered during processing\n"
            "  },\n"
            "  \"medium\" : { ... },    (json object, optional) estimate for medium time horizon\n"
            "  \"long\" : { ... }       (json object) estimate for long time horizon\n"
            "}\n"
            "\n"
            "Results are returned for any horizon which tracks blocks up to the confirmation target.\n"
            "\nExample:\n"
            + HelpExampleCli("estimaterawfee", "6 0.9")
            );

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VNUM}, true);
    RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
    unsigned int conf_target = ParseConfirmTarget(request.params[0]);
    double threshold = 0.95;
    if (!request.params[1].isNull()) {
        threshold = request.params[1].get_real();
    }
    if (threshold < 0 || threshold > 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid threshold");
    }

    UniValue result(UniValue::VOBJ);

    for (FeeEstimateHorizon horizon : {FeeEstimateHorizon::SHORT_HALFLIFE, FeeEstimateHorizon::MED_HALFLIFE, FeeEstimateHorizon::LONG_HALFLIFE}) {
        CFeeRate feeRate;
        EstimationResult buckets;

        // Only output results for horizons which track the target
        if (conf_target > ::feeEstimator.HighestTargetTracked(horizon)) continue;

        feeRate = ::feeEstimator.estimateRawFee(conf_target, threshold, horizon, &buckets);
        UniValue horizon_result(UniValue::VOBJ);
        UniValue errors(UniValue::VARR);
        UniValue passbucket(UniValue::VOBJ);
        passbucket.pushKV("startrange", round(buckets.pass.start));
        passbucket.pushKV("endrange", round(buckets.pass.end));
        passbucket.pushKV("withintarget", round(buckets.pass.withinTarget * 100.0) / 100.0);
        passbucket.pushKV("totalconfirmed", round(buckets.pass.totalConfirmed * 100.0) / 100.0);
        passbucket.pushKV("inmempool", round(buckets.pass.inMempool * 100.0) / 100.0);
        passbucket.pushKV("leftmempool", round(buckets.pass.leftMempool * 100.0) / 100.0);
        UniValue failbucket(UniValue::VOBJ);
        failbucket.pushKV("startrange", round(buckets.fail.start));
        failbucket.pushKV("endrange", round(buckets.fail.end));
        failbucket.pushKV("withintarget", round(buckets.fail.withinTarget * 100.0) / 100.0);
        failbucket.pushKV("totalconfirmed", round(buckets.fail.totalConfirmed * 100.0) / 100.0);
        failbucket.pushKV("inmempool", round(buckets.fail.inMempool * 100.0) / 100.0);
        failbucket.pushKV("leftmempool", round(buckets.fail.leftMempool * 100.0) / 100.0);

        // CFeeRate(0) is used to indicate error as a return value from estimateRawFee
        if (feeRate != CFeeRate(0)) {
            horizon_result.pushKV("feerate", ValueFromAmount(feeRate.GetFeePerK()));
            horizon_result.pushKV("decay", buckets.decay);
            horizon_result.pushKV("scale", (int)buckets.scale);
            horizon_result.pushKV("pass", passbucket);
            // buckets.fail.start == -1 indicates that all buckets passed, there is no fail bucket to output
            if (buckets.fail.start != -1) horizon_result.pushKV("fail", failbucket);
        } else {
            // Output only information that is still meaningful in the event of error
            horizon_result.pushKV("decay", buckets.decay);
            horizon_result.pushKV("scale", (int)buckets.scale);
            horizon_result.pushKV("fail", failbucket);
            errors.push_back("Insufficient data or no feerate found which meets threshold");
            horizon_result.pushKV("errors",errors);
        }
        result.pushKV(StringForFeeEstimateHorizon(horizon), horizon_result);
    }
    return result;
}

UniValue combineblocksigs(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "combineblocksigs \"blockhex\" \"signature\"\n"
            "\nMerges signatures on a block proposal\n"
            "\nArguments:\n"
            "1. \"blockhex\"       (string, required) The hex-encoded block from getnewblockhex\n"
            "2. \"signature\"      (string, required) A block signature from aggregate pubkey (in the form of a hex-encoded scriptSig)\n"
            "\nResult\n"
            "{\n"
            "  \"hex\": \"value\",       (string) The signed block\n"
            "  \"complete\": n           (numeric) if block is complete \n"
            "  \"warning\": \"message\"  (string) diagnostic message stating why signatures were not added to a block \n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("combineblocksigs", "<hex> \"signature\"")
        );

    CBlock block;
    if (!DecodeHexBlk(block, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    const std::string signature(request.params[1].get_str());
    if(!signature.size())
        throw JSONRPCError(RPC_INVALID_PARAMS, "Signature was empty");

    if (!IsHex(signature))
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid signature");

    const std::vector<unsigned char> blockProof(ParseHex(signature));

    if(blockProof.size() != CPubKey::SCHNORR_SIGNATURE_SIZE || !CheckSchnorrSignatureEncoding(blockProof, nullptr, true) )
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid signature encoding");

    XFieldAggPubKey aggpubkeyChange;
    CXFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);
    CPubKey aggpubkey(aggpubkeyChange.getPubKey());

    bool status = block.AbsorbBlockProof(blockProof, aggpubkey);

    UniValue result(UniValue::VOBJ);
    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << block;

    result.push_back(Pair("hex", HexStr(ssBlock.begin(), ssBlock.end())));
    result.push_back(Pair("complete", status));

    return result;
}

UniValue testproposedblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1 )
        throw std::runtime_error(
            "testproposedblock \"blockhex\"\n"
            "\nValidate proposed block before signing\n"
            "\nArguments:\n"
            "1. \"blockhex\"       (string, required) The hex-encoded block from getnewblockhex\n"
            "\nResult\n"
            "\"valid\"              (bool) true when the block is valid, JSON exception on failure\n"
            "\nExamples:\n"
            + HelpExampleCli("testproposedblock", "\"blockhex\"")
        );

    CBlock block;
    if (!DecodeHexBlk(block, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    LOCK(cs_main);

    //test block validity
    uint256 hash = block.GetHash();
    const CBlockIndex* pindex = LookupBlockIndex(hash);
    if (pindex)
    {
        if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
            throw JSONRPCError(RPC_VERIFY_ERROR,  "duplicate");

        if (pindex->nStatus & BLOCK_FAILED_MASK)
            throw JSONRPCError(RPC_VERIFY_ERROR,  "duplicate-invalid");

    }

    CBlockIndex* const pindexPrev = chainActive.Tip();
    // TestBlockValidity only supports blocks built on the current Tip
    if (block.hashPrevBlock != pindexPrev->GetBlockHash())
        throw JSONRPCError(RPC_VERIFY_ERROR, "proposal was not based on our best chain");

    CValidationState state;

    bool valid = TestBlockValidity(state, block, pindexPrev, false, true);
    if (!valid || !state.IsValid()) {
        std::string strRejectReason = state.GetRejectReason();
        if (strRejectReason.empty())
            throw JSONRPCError(RPC_VERIFY_ERROR, state.IsInvalid() ? "Block proposal was invalid" : "Error checking block proposal");
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    }

    for (auto& transaction : block.vtx) {
        if (transaction->IsCoinBase()) continue;
        std::string reason;
        if (!IsStandardTx(*transaction, reason)) {
            throw JSONRPCError(RPC_VERIFY_ERROR, "Block proposal included a non-standard transaction: " + reason);
        }
    }
    return valid ? true : false;
}
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "mining",             "getmininginfo",          &getmininginfo,          {} },
    { "mining",             "getblocktemplate",       &getblocktemplate,       {"template_request"} },
    { "mining",             "submitblock",            &submitblock,            {"hexdata","dummy"} },


    { "generating",         "generatetoaddress",      &generatetoaddress,      {"nblocks","address", "privkeys"} },
    { "generating",         "getnewblock",            &getnewblock,            {"address", "required_age"} },

    { "hidden",             "estimatefee",            &estimatefee,            {} },
    { "util",               "estimatesmartfee",       &estimatesmartfee,       {"conf_target", "estimate_mode"} },

    { "hidden",             "estimaterawfee",         &estimaterawfee,         {"conf_target", "threshold"} },
    { "mining",             "combineblocksigs",       &combineblocksigs,       {"block_hex", "signature_list"} },
    { "mining",             "testproposedblock",      &testproposedblock,       {"block_hex", "nonstandard"} },
};

void RegisterMiningRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
