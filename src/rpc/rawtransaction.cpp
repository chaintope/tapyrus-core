// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <coins.h>
#include <coloridentifier.h>
#include <compat/byteswap.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <index/txindex.h>
#include <keystore.h>
#include <validation.h>
#include <validationinterface.h>
#include <key_io.h>
#include <merkleblock.h>
#include <net.h>
#include <policy/packages.h>
#include <policy/rbf.h>
#include <primitives/transaction.h>
#include <pstt.h>
#include <rpc/protocol.h>
#include <rpc/rawtransaction.h>
#include <rpc/server.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script/standard.h>
#include <softforkmanager.h>
#include <txmempool.h>
#include <uint256.h>
#include <utilstrencodings.h>
#include <file_io.h>

#if ENABLE_WALLET
#include <wallet/rpcwallet.h>
#endif

#include <future>
#include <stdint.h>

#include <univalue.h>


static void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    // Call into TxToUniv() in tapyrus-common to decode the transaction hex.
    //
    // Blockchain contextual information (confirmations and blocktime) is not
    // available to code in tapyrus-common, so we query them here and push the
    // data into the returned UniValue.
    TxToUniv(tx, uint256(), entry, true, RPCSerializationFlags());

    if (!hashBlock.IsNull()) {
        LOCK(cs_main);

        entry.pushKV("blockhash", hashBlock.GetHex());
        CBlockIndex* pindex = LookupBlockIndex(hashBlock);
        if (pindex) {
            if (chainActive.Contains(pindex)) {
                entry.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
            }
            else
                entry.pushKV("confirmations", 0);
        }
    }
}

static UniValue getrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose \"blockhash\" )\n"

            "\nNOTE: By default this function only works for mempool transactions. If the -txindex option is\n"
            "enabled, it also works for blockchain transactions. If the block which contains the transaction\n"
            "is known, its hash can be provided even for nodes without -txindex. Note that if a blockhash is\n"
            "provided, only that block will be searched and if the transaction is in the mempool or other\n"
            "blocks, or if this node does not have the given block available, the transaction will not be found.\n"
            "DEPRECATED: for now, it also works for transactions with unspent outputs.\n"

            "\nReturn the raw transaction data.\n"
            "\nIf verbose is 'true', returns an Object with information about 'txid'.\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose     (bool, optional, default=false) If false, return a string, otherwise return a json object\n"
            "3. \"blockhash\" (string, optional) The block in which to look for the transaction\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"in_active_chain\": b, (bool) Whether specified block is in the active chain or not (only present with explicit \"blockhash\" argument)\n"
            "  \"hex\" : \"data\",       (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",        (string) The transaction id (same as provided)\n"
            "  \"hash\" : \"id\",        (string) The transaction hash including scriptSig (differs from txid).\n"
            "  \"size\" : n,             (numeric) The serialized transaction size\n"
            "  \"features\" : n,         (numeric) The transaction features\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"token\" : \"color\" ,      (string) Color Identifier for tokens or " + CURRENCY_UNIT + "\n"
            "       \"value\" : x.xxx,           (numeric) The transaction value in the token stated above\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"address\"        (string) tapyrus address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) The confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", true")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" false \"myblockhash\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true \"myblockhash\"")
        );

    bool in_active_chain = true;
    uint256 hash = ParseHashV(request.params[0], "parameter 1");
    CBlockIndex* blockindex = nullptr;

    if (hash == FederationParams().GenesisBlock().hashMerkleRoot) {
        // Special exception for the genesis block coinbase transaction
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "The genesis block coinbase is not considered an ordinary transaction and cannot be retrieved");
    }

    // Accept either a bool (true) or a num (>=1) to indicate verbose output.
    bool fVerbose = false;
    if (!request.params[1].isNull()) {
        fVerbose = request.params[1].isNum() ? (request.params[1].get_int() != 0) : request.params[1].get_bool();
    }

    if (!request.params[2].isNull()) {
        LOCK(cs_main);

        uint256 blockhash = ParseHashV(request.params[2], "parameter 3");
        blockindex = LookupBlockIndex(blockhash);
        if (!blockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
        }
        in_active_chain = chainActive.Contains(blockindex);
    }

    bool f_txindex_ready = false;
    if (g_txindex && !blockindex) {
        f_txindex_ready = g_txindex->BlockUntilSyncedToCurrentChain();
    }

    CTransactionRef tx;
    uint256 hash_block;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hash_block, true, blockindex)) {
        std::string errmsg;
        if (blockindex) {
            if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
            }
            errmsg = "No such transaction found in the provided block";
        } else if (!g_txindex) {
            errmsg = "No such mempool transaction. Use -txindex to enable blockchain transaction queries";
        } else if (!f_txindex_ready) {
            errmsg = "No such mempool transaction. Blockchain transactions are still in the process of being indexed";
        } else {
            errmsg = "No such mempool or blockchain transaction";
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg + ". Use gettransaction for wallet transactions.");
    }

    if (!fVerbose) {
        return EncodeHexTx(*tx, RPCSerializationFlags());
    }

    UniValue result(UniValue::VOBJ);
    if (blockindex) result.pushKV("in_active_chain", in_active_chain);
    TxToJSON(*tx, hash_block, result);
    return result;
}

static UniValue gettxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2))
        throw std::runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included manually (by blockhash).\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction id (transaction hash without scriptSig)\n"
            "      ,...\n"
            "    ]\n"
            "2. \"blockhash\"   (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof.\n"
        );

    std::set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = request.params[0].get_array();
    for (unsigned int idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid txid ")+txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated txid: ")+txid.get_str());
       setTxids.insert(hash);
       oneTxid = hash;
    }

    CBlockIndex* pblockindex = nullptr;
    uint256 hashBlock;
    if (!request.params[1].isNull()) {
        LOCK(cs_main);
        hashBlock = uint256S(request.params[1].get_str());
        pblockindex = LookupBlockIndex(hashBlock);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    } else {
        LOCK(cs_main);

        // Loop through txids and try to find which block they're in. Exit loop once a block is found.
        for (const auto& tx : setTxids) {
            const Coin& coin = AccessByTxid(*pcoinsTip, tx);
            if (!coin.IsSpent()) {
                pblockindex = chainActive[coin.nHeight];
                break;
            }
        }
    }


    // Allow txindex to catch up if we need to query it and before we acquire cs_main.
    if (g_txindex && !pblockindex) {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    LOCK(cs_main);

    if (pblockindex == nullptr)
    {
        CTransactionRef tx;
        if (!GetTransaction(oneTxid, tx, Params().GetConsensus(), hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        pblockindex = LookupBlockIndex(hashBlock);
        if (!pblockindex) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        }
    }

    CBlock block;
    if(!ReadBlockFromDisk(block, pblockindex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    for (const auto& tx : block.vtx)
        if (setTxids.count(tx->GetHashMalFix()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not all transactions found in specified or retrieved block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

static UniValue verifytxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof can not be validated.\n"
        );

    CDataStream ssMB(ParseHexV(request.params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    std::vector<uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashImMerkleRoot)
        return res;

    LOCK(cs_main);

    const CBlockIndex* pindex = LookupBlockIndex(merkleBlock.header.GetHash());
    if (!pindex || !chainActive.Contains(pindex) || pindex->nTx == 0) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");
    }

    // Check if proof is valid, only add results if so
    if (pindex->nTx == merkleBlock.txn.GetNumTransactions()) {
        for (const uint256& hash : vMatch) {
            res.push_back(hash.GetHex());
        }
    }

    return res;
}

// Builds the CTxOut for one already-decoded destination + amount value, the
// shared tail end of ConstructTransaction's per-output-key loop below.
// Factored out so PSTT's addoutputtopstt/addinputoutputpairtopstt (which add
// exactly one output at a time, not a whole array/dict) can reuse the same
// color-aware destination/amount handling instead of duplicating it.
static CTxOut BuildDestinationTxOut(const CTxDestination& destination, const UniValue& value)
{
    ColorIdentifier colorId;
    if (destination.index() == 3)
        colorId = std::get<CColorKeyID>(destination).color;
    else if (destination.index() == 4)
        colorId = std::get<CColorScriptID>(destination).color;

    CScript scriptPubKey = GetScriptForDestination(destination);
    CAmount nAmount = (colorId.type == TokenTypes::NONE ? AmountFromValue(value) : TokenAmountFromValue(value));
    return CTxOut(nAmount, scriptPubKey);
}

CMutableTransaction ConstructTransaction(const UniValue& inputs_in, const UniValue& outputs_in, const UniValue& locktime, const UniValue& rbf)
{
    if (inputs_in.isNull() || outputs_in.isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = inputs_in.get_array();
    const bool outputs_is_obj = outputs_in.isObject();
    UniValue outputs = outputs_is_obj ? outputs_in.get_obj() : outputs_in.get_array();

    CMutableTransaction rawTx;

    if (!locktime.isNull()) {
        int64_t nLockTime = locktime.get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

    bool rbfOptIn = rbf.isTrue();

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = o.find_value("vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence;
        if (rbfOptIn) {
            nSequence = MAX_BIP125_RBF_SEQUENCE;
        } else if (rawTx.nLockTime) {
            nSequence = std::numeric_limits<uint32_t>::max() - 1;
        } else {
            nSequence = std::numeric_limits<uint32_t>::max();
        }

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = o.find_value("sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            } else {
                nSequence = (uint32_t)seqNr64;
            }
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    std::set<CTxDestination> destinations;
    if (!outputs_is_obj) {
        // Translate array of key-value pairs into dict
        UniValue outputs_dict = UniValue(UniValue::VOBJ);
        for (size_t i = 0; i < outputs.size(); ++i) {
            const UniValue& output = outputs[i];
            if (!output.isObject()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, key-value pair not an object as expected");
            }
            if (output.size() != 1) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, key-value pair must contain exactly one key");
            }
            outputs_dict.pushKVs(output);
        }
        outputs = std::move(outputs_dict);
    }
    for (const std::string& name_ : outputs.getKeys()) {
        if (name_ == "data") {
            std::vector<unsigned char> data = ParseHexV(outputs[name_].getValStr(), "Data");

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CTxDestination destination = DecodeDestination(name_);
            if (!IsValidDestination(destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Tapyrus address: ") + name_);
            }

            if (!destinations.insert(destination).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
            }
            rawTx.vout.push_back(BuildDestinationTxOut(destination, outputs[name_]));
        }
    }

    if (!rbf.isNull() && rawTx.vin.size() > 0 && rbfOptIn != SignalsOptInRBF(rawTx)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter combination: Sequence number(s) contradict replaceable option");
    }

    return rawTx;
}

// Mirrors ConstructTransaction's per-input CTxIn construction (default
// sequence based on RBF opt-in / whether a locktime is set, with an explicit
// per-input override otherwise) -- but reads previous_txid/output_index,
// PSTT's own field vocabulary (matching PSTT_IN_PREVIOUS_TXID/
// PSTT_IN_OUTPUT_INDEX and addinputtopstt's own param names), rather than
// ConstructTransaction's txid/vout (createrawtransaction's vocabulary).
// Used by createpstt/walletcreatefundedpstt instead of passing their inputs
// array through ConstructTransaction directly, which would otherwise throw
// on every PSTT-shaped input object.
std::vector<CTxIn> ParsePsttInputEntries(const UniValue& inputs_in, uint32_t nLockTime, bool rbfOptIn)
{
    UniValue inputs = inputs_in.get_array();
    std::vector<CTxIn> vin;
    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& o = inputs[idx].get_obj();

        uint256 txid = ParseHashO(o, "previous_txid");

        const UniValue& vout_v = o.find_value("output_index");
        if (!vout_v.isNum()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing output_index key");
        }
        int nOutput = vout_v.get_int();
        if (nOutput < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, output_index must be positive");
        }

        uint32_t nSequence;
        if (rbfOptIn) {
            nSequence = MAX_BIP125_RBF_SEQUENCE;
        } else if (nLockTime) {
            nSequence = std::numeric_limits<uint32_t>::max() - 1;
        } else {
            nSequence = std::numeric_limits<uint32_t>::max();
        }

        const UniValue& sequenceObj = o.find_value("sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            }
            nSequence = (uint32_t)seqNr64;
        }

        vin.emplace_back(COutPoint(txid, (uint32_t)nOutput), CScript(), nSequence);
    }
    return vin;
}

static UniValue createrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4) {
        throw std::runtime_error(
            // clang-format off
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] [{\"address\":amount},{\"data\":\"hex\"},...] ( locktime ) ( replaceable )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs.\n"
            "Outputs can be addresses or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",      (string, required) The transaction id\n"
            "         \"vout\":n,         (numeric, required) The output number\n"
            "         \"sequence\":n      (numeric, optional) The sequence number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"               (array, required) a json array with outputs (key-value pairs)\n"
            "   [\n"
            "    {\n"
            "      \"address\": x.xxx,    (obj, optional) A key-value pair. The key (string) is the tapyrus address, the value (float or string) is the amount in " + CURRENCY_UNIT + "\n"
            "    },\n"
            "    {\n"
            "      \"data\": \"hex\"        (obj, optional) A key-value pair. The key must be \"data\", the value is hex encoded data\n"
            "    }\n"
            "    ,...                     More key-value pairs of the above form. For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also\n"
            "                             accepted as second parameter.\n"
            "   ]\n"
            "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
            "4. replaceable               (boolean, optional, default=false) Marks this transaction as BIP125 replaceable.\n"
            "                             Allows this transaction to be replaced by a transaction with higher fees. If provided, it is an error if explicit sequence numbers are incompatible.\n"
            "\nResult:\n"
            "\"transaction\"              (string) hex string of the transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"address\\\":0.01}]\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"data\\\":\\\"00010203\\\"}]\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"[{\\\"address\\\":0.01}]\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"[{\\\"data\\\":\\\"00010203\\\"}]\"")
            // clang-format on
        );
    }

    RPCTypeCheck(request.params, {
        UniValue::VARR,
        UniValueType(), // ARR or OBJ, checked later
        UniValue::VNUM,
        UniValue::VBOOL
        }, true
    );

    CMutableTransaction rawTx = ConstructTransaction(request.params[0], request.params[1], request.params[2], request.params[3]);

    return EncodeHexTx(rawTx);
}

static UniValue decoderawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (including scriptSig)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"features\" : n,          (numeric) The transaction features\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"token\" : \"color\" ,      (string) Color Identifier for tokens or " + CURRENCY_UNIT + "\n"
            "       \"value\" : x.xxx,           (numeric) The transaction value in the token stated above\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"12tvKAXCxZjSmdNbao16dKXC8tRWfcF5oc\"   (string) tapyrus address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    CMutableTransaction mtx;

    if (!DecodeHexTx(mtx, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    UniValue result(UniValue::VOBJ);
    TxToUniv(CTransaction(std::move(mtx)), uint256(), result, false);

    return result;
}

static UniValue decodescript(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodescript \"hexstring\"\n"
            "\nDecode a hex-encoded script.\n"
            "\nArguments:\n"
            "1. \"hexstring\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) tapyrus address\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\",\"address\" (string) address of P2SH script wrapping this redeem script (not returned if the script is already a P2SH).\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"hexstring\"")
            + HelpExampleRpc("decodescript", "\"hexstring\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (request.params[0].get_str().size() > 0){
        std::vector<unsigned char> scriptData(ParseHexV(request.params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToUniv(script, r, false);

    UniValue type;
    type = r.find_value("type");

    if (type.isStr() && type.get_str() != "scripthash" && type.get_str() != "coloredscripthash") {
        // P2SH cannot be wrapped in a P2SH. If this script is already a P2SH,
        // don't return the address for a P2SH of the P2SH.
        r.pushKV("p2sh", EncodeDestination(CScriptID(script)));
        if (type.get_str() == "pubkey" || type.get_str() == "pubkeyhash" || type.get_str() == "multisig" || type.get_str() == "coloredpubkeyhash" || type.get_str() == "nonstandard") {
            txnouttype which_type;
            std::vector<std::vector<unsigned char>> solutions_data;
            Solver(script, which_type, solutions_data);
            if (which_type == TX_COLOR_PUBKEYHASH)
                r.pushKV("token", GetColorIdFromScript(script).toHexString());
        }
    }

    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("txid", txin.prevout.hashMalFix.ToString());
    entry.pushKV("vout", (uint64_t)txin.prevout.n);
    entry.pushKV("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
    entry.pushKV("sequence", (uint64_t)txin.nSequence);
    entry.pushKV("error", strMessage);
    vErrorsRet.push_back(entry);
}

static UniValue combinerawtransaction(const JSONRPCRequest& request)
{

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "combinerawtransaction [\"hexstring\",...]\n"
            "\nCombine multiple partially signed transactions into one transaction.\n"
            "The combined transaction may be another partially signed transaction or a \n"
            "fully signed transaction."

            "\nArguments:\n"
            "1. \"txs\"         (string) A json array of hex strings of partially signed transactions\n"
            "    [\n"
            "      \"hexstring\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "\"hex\"            (string) The hex-encoded raw transaction with signature(s)\n"

            "\nExamples:\n"
            + HelpExampleCli("combinerawtransaction", "[\"myhex1\", \"myhex2\", \"myhex3\"]")
        );


    UniValue txs = request.params[0].get_array();
    std::vector<CMutableTransaction> txVariants(txs.size());

    for (unsigned int idx = 0; idx < txs.size(); idx++) {
        if (!DecodeHexTx(txVariants[idx], txs[idx].get_str())) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed for tx %d", idx));
        }
    }

    if (txVariants.empty()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transactions");
    }

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(cs_main);
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mergedTx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    const unsigned int signVerifyFlags = STANDARD_SCRIPT_VERIFY_FLAGS;

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            throw JSONRPCError(RPC_VERIFY_ERROR, "Input not found or already spent");
        }
        SignatureData sigdata;

        // ... and merge in other signatures:
        for (const CMutableTransaction& txv : txVariants) {
            if (txv.vin.size() > i) {
                sigdata.MergeSignatureData(DataFromTransaction(txv, i, coin.out));
            }
        }
        ProduceSignature(DUMMY_SIGNING_PROVIDER, MutableTransactionSignatureCreator(&mergedTx, i, coin.out.nValue, 1), coin.out.scriptPubKey, sigdata, signVerifyFlags);

        UpdateInput(txin, sigdata);
    }

    return EncodeHexTx(mergedTx);
}

UniValue SignTransaction(CMutableTransaction& mtx, const UniValue& prevTxsUnival, CBasicKeyStore *keystore, bool is_temp_keystore, const UniValue& hashType, const SignatureScheme sigScheme)
{
    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK2(cs_main, mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : mtx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Add previous txouts given in the RPC call:
    if (!prevTxsUnival.isNull()) {
        UniValue prevTxs = prevTxsUnival.get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); ++idx) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject()) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");
            }

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                {
                    {"txid", UniValueType(UniValue::VSTR)},
                    {"vout", UniValueType(UniValue::VNUM)},
                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = prevOut.find_value("vout").get_int();
            if (nOut < 0) {
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");
            }

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                const Coin& coin = view.AccessCoin(out);
                if (!coin.IsSpent() && coin.out.scriptPubKey != scriptPubKey) {
                    std::string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coin.out.scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                Coin newcoin;
                newcoin.out.scriptPubKey = scriptPubKey;
                newcoin.out.nValue = MAX_MONEY;
                if (prevOut.exists("amount")) {
                    newcoin.out.nValue = AmountFromValue(prevOut.find_value("amount"));
                }
                newcoin.nHeight = 1;
                view.AddCoin(out, std::move(newcoin), true);
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the keystore so it can be signed:
            if (is_temp_keystore && (scriptPubKey.IsPayToScriptHash() || scriptPubKey.IsColoredPayToScriptHash())) {
                RPCTypeCheckObj(prevOut,
                    {
                        {"redeemScript", UniValueType(UniValue::VSTR)},
                    });
                UniValue v = prevOut.find_value("redeemScript");
                if (!v.isNull()) {
                    std::vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    keystore->AddCScript(redeemScript);
                }
            }
        }
    }

    int nHashType = ParseSighashString(hashType);

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    const unsigned int signVerifyFlags = STANDARD_SCRIPT_VERIFY_FLAGS;

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    TxColoredCoinBalancesMap inBalances, outBalances;
    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mtx);
    // Sign what we can:
    for (unsigned int i = 0; i < mtx.vin.size(); i++) {
        CTxIn& txin = mtx.vin[i];
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coin.out.scriptPubKey;
        const CAmount& amount = coin.out.nValue;
        inBalances[GetColorIdFromScript(coin.out.scriptPubKey)] += amount;

        SignatureData sigdata = DataFromTransaction(mtx, i, coin.out);
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mtx.vout.size())) {
            ProduceSignature(*keystore, MutableTransactionSignatureCreator(&mtx, i, amount, nHashType, sigScheme), prevPubKey, sigdata, signVerifyFlags);
        }

        UpdateInput(txin, sigdata);

        ColorIdentifier tempColorId;
        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, signVerifyFlags, TransactionSignatureChecker(&txConst, i, amount), tempColorId, &serror)) {
            if (serror == SCRIPT_ERR_INVALID_STACK_OPERATION) {
                // Unable to sign input and verification failed (possible attempt to partially sign).
                TxInErrorToJSON(txin, vErrors, "Unable to sign input, invalid stack size (possibly missing key)");
            } else {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
    }
    bool fComplete = vErrors.empty();

    //add a warning for token burn
    bool burn = false;
    for(const auto& out : mtx.vout)
        outBalances[GetColorIdFromScript(out.scriptPubKey)] += out.nValue;

    for(const auto& in:inBalances)
        if(in.first.type != TokenTypes::NONE && in.second > outBalances[in.first])
        {
            burn = true;
            break;
        }
    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(mtx));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty()) {
        result.pushKV("errors", vErrors);
    }
    if(burn)
        result.pushKV("warning", "token burn detected");

    return result;
}

static UniValue signrawtransactionwithkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 5)
        throw std::runtime_error(
            "signrawtransactionwithkey \"hexstring\" [\"privatekey1\",...] ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second argument is an array of private keys in WIF\n"
            "that will be the only keys used to sign the transaction.\n"
            "The third optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"

            "\nArguments:\n"
            "1. \"hexstring\"                      (string, required) The transaction hex string\n"
            "2. \"privkeys\"                       (string, required) A json array of private keys for signing\n"
            "    [                               (json array of strings)\n"
            "      \"privatekey\"                  (string) private key in WIF (Wallet Import Format, see dumpprivkey)\n"
            "      ,...\n"
            "    ]\n"
            "3. \"prevtxs\"                        (string, optional) An json array of previous dependent transaction outputs\n"
            "     [                              (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",               (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",     (string, required) script key\n"
            "         \"redeemScript\": \"hex\",     (string, required for P2SH or P2WSH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "4. \"sighashtype\"                    (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"
            "5. \"sigscheme\"                    (string, optional, default=ECDSA) The signature scheme to use for this transaction\n"
            "       \"ECDSA\"\n"
            "       \"SCHNORR\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",                  (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,          (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                      (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"id\",              (string) The transaction Id of the referenced, previous transaction\n"
            "      \"vout\" : n,                   (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",          (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,               (numeric) Script sequence number\n"
            "      \"error\" : \"text\"              (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransactionwithkey", "\"myhex\"")
            + HelpExampleRpc("signrawtransactionwithkey", "\"myhex\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VARR, UniValue::VSTR}, true);

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    CBasicKeyStore keystore;
    const UniValue& keys = request.params[1].get_array();
    for (unsigned int idx = 0; idx < keys.size(); ++idx) {
        UniValue k = keys[idx];
        CKey key = DecodeSecret(k.get_str());
        if (!key.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
        }
        keystore.AddKey(key);
    }

    SignatureScheme sigScheme = ParseSigSchemeString(request.params[4]);

    return SignTransaction(mtx, request.params[2], &keystore, true, request.params[3], sigScheme);
}

UniValue signrawtransaction(const JSONRPCRequest& request)
{
#if ENABLE_WALLET
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
#endif

    if (request.fHelp || request.params.size() < 1 || request.params.size() > 5)
        throw std::runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nDEPRECATED. Sign inputs for raw transaction (serialized, hex-encoded).\n"
            "The second optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain.\n"
            "The third optional argument (may be null) is an array of private\n"
            "keys in WIF that, if given, will be the only keys used to sign the transaction.\n"
#if ENABLE_WALLET
            + HelpRequiringPassphrase(pwallet) + "\n"
#endif
            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\",   (string, required for P2SH or P2WSH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privkeys\"     (string, optional) A json array of private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in WIF (Wallet Import Format, see dumpprivkey)\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"
            "5. \"sigscheme\"                    (string, optional, default=ECDSA) The signature scheme to use for this transaction\n"
            "       \"ECDSA\"\n"
            "       \"SCHNORR\"\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"Id\",           (string) The transaction Id of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

    if (!IsDeprecatedRPCEnabled("signrawtransaction")) {
        throw JSONRPCError(RPC_METHOD_DEPRECATED, "signrawtransaction is deprecated and will be fully removed in v0.18. "
            "To use signrawtransaction in v0.17, restart tapyrusd with -deprecatedrpc=signrawtransaction.\n"
            "Projects should transition to using signrawtransactionwithkey and signrawtransactionwithwallet before upgrading to v0.18");
    }

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VARR, UniValue::VSTR}, true);

    // Make a JSONRPCRequest to pass on to the right signrawtransaction* command
    JSONRPCRequest new_request;
    new_request.id = request.id;
    new_request.params.setArray();

    // For signing with private keys
    if (!request.params[2].isNull()) {
        new_request.params.push_back(request.params[0]);
        // Note: the prevtxs and privkeys are reversed for signrawtransactionwithkey
        new_request.params.push_back(request.params[2]);
        new_request.params.push_back(request.params[1]);
        new_request.params.push_back(request.params[3]);
        new_request.params.push_back(request.params[4]);
        return signrawtransactionwithkey(new_request);
    } else {
#if ENABLE_WALLET
        // Otherwise sign with the wallet which does not take a privkeys parameter
        new_request.params.push_back(request.params[0]);
        new_request.params.push_back(request.params[1]);
        new_request.params.push_back(request.params[3]);
        new_request.params.push_back(request.params[4]);
        return signrawtransactionwithwallet(new_request);
#else
        // If we have made it this far, then wallet is disabled and no private keys were given, so fail here.
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No private keys available.");
#endif
    }
}

static UniValue sendrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction Id in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );

    std::promise<void> promise;

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL});

    // parse hex string from parameter
    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint256& hashTx = tx->GetHashMalFix();

    CAmount nMaxRawTxFee = maxTxFee;
    if (!request.params[1].isNull() && request.params[1].get_bool())
        nMaxRawTxFee = 0;

    { // cs_main scope
    LOCK(cs_main);
    CCoinsViewCache &view = *pcoinsTip;
    bool fHaveChain = false;
    for (size_t o = 0; !fHaveChain && o < tx->vout.size(); o++) {
        const Coin& existingCoin = view.AccessCoin(COutPoint(hashTx, o));
        fHaveChain = !existingCoin.IsSpent();
    }
    bool fHaveMempool = mempool.exists(hashTx);
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CTxMempoolAcceptanceOptions opt;
        opt.nAbsurdFee = nMaxRawTxFee;
        if (!AcceptToMemoryPool(std::move(tx), opt)) {
            if (opt.state.IsInvalid()) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, FormatStateMessage(opt.state));
            } else {
                if (opt.missingInputs.size()) {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, FormatStateMessage(opt.state));
            }
        } else {
            // If wallet is enabled, ensure that the wallet has been made aware
            // of the new transaction prior to returning. This prevents a race
            // where a user might call sendrawtransaction with a transaction
            // to/from their wallet, immediately call some wallet RPC, and get
            // a stale result because callbacks have not yet been processed.
            CallFunctionInValidationInterfaceQueue([&promise] {
                promise.set_value();
            });
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    } else {
        // Make sure we don't block forever if re-sending
        // a transaction already in mempool.
        promise.set_value();
    }

    } // cs_main

    promise.get_future().wait();

    if(!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    CInv inv(MSG_TX, hashTx);
    g_connman->ForEachNode([&inv](CNode* pnode)
    {
        pnode->PushInventory(inv);
    });

    return hashTx.GetHex();
}

static std::string WriteHDKeypath(std::vector<uint32_t>& keypath)
{
    std::string keypath_str = "m";
    for (uint32_t num : keypath) {
        keypath_str += "/";
        bool hardened = false;
        if (num & 0x80000000) {
            hardened = true;
            num &= ~0x80000000;
        }

        keypath_str += std::to_string(num);
        if (hardened) {
            keypath_str += "'";
        }
    }
    return keypath_str;
}

// ---------------------------------------------------------------------
// PSTT (TIP-174) -- see doc/tapyrus/pstt.md
// ---------------------------------------------------------------------

// Mirrors addTokenKV's (rpcwallet.cpp) token/amount display convention, but
// keyed off a scriptPubKey directly rather than a CTxDestination, matching
// decision #15 in doc/tapyrus/pstt.md: color is always derived from the
// script, never assumed from context.
static void PushTokenAmount(const CScript& script, CAmount amount, UniValue& entry)
{
    ColorIdentifier colorId = GetColorIdFromScript(script);
    entry.pushKV("token", colorId.toHexString());
    entry.pushKV("amount", (colorId.type == TokenTypes::NONE ? ValueFromAmount(amount) : amount));
}

static void PsttInputToUniv(const PSTTInput& input, UniValue& in)
{
    in.pushKV("previous_txid", input.previous_txid.GetHex());
    in.pushKV("output_index", (uint64_t)input.prev_out_index);

    if (input.utxo) {
        UniValue utxo_univ(UniValue::VOBJ);
        TxToUniv(*input.utxo, uint256(), utxo_univ, false);
        in.pushKV("utxo", utxo_univ);
        if (input.prev_out_index < input.utxo->vout.size()) {
            const CTxOut& out = input.utxo->vout[input.prev_out_index];
            PushTokenAmount(out.scriptPubKey, out.nValue, in);
        }
    }

    if (!input.partial_sigs.empty()) {
        UniValue partial_sigs(UniValue::VOBJ);
        for (const auto& sig : input.partial_sigs) {
            partial_sigs.pushKV(HexStr(sig.second.first), HexStr(sig.second.second));
        }
        in.pushKV("partial_signatures", partial_sigs);
    }

    if (input.sighash_type) {
        in.pushKV("sighash", SighashToStr((unsigned char)*input.sighash_type));
    }

    if (!input.redeem_script.empty()) {
        UniValue r(UniValue::VOBJ);
        ScriptToUniv(input.redeem_script, r, false);
        in.pushKV("redeem_script", r);
    }

    if (!input.hd_keypaths.empty()) {
        UniValue keypaths(UniValue::VARR);
        for (auto entry : input.hd_keypaths) {
            UniValue keypath(UniValue::VOBJ);
            keypath.pushKV("pubkey", HexStr(entry.first));
            uint32_t fingerprint = entry.second.at(0);
            keypath.pushKV("master_fingerprint", strprintf("%08x", internal_bswap_32(fingerprint)));
            entry.second.erase(entry.second.begin());
            keypath.pushKV("path", WriteHDKeypath(entry.second));
            keypaths.push_back(keypath);
        }
        in.pushKV("bip32_derivs", keypaths);
    }

    if (!input.final_script_sig.empty()) {
        UniValue scriptsig(UniValue::VOBJ);
        scriptsig.pushKV("asm", ScriptToAsmStr(input.final_script_sig, true));
        scriptsig.pushKV("hex", HexStr(input.final_script_sig));
        in.pushKV("final_scriptSig", scriptsig);
    }

    if (input.sequence) in.pushKV("sequence", (uint64_t)*input.sequence);
    if (input.required_time_locktime) in.pushKV("required_time_locktime", (uint64_t)*input.required_time_locktime);
    if (input.required_height_locktime) in.pushKV("required_height_locktime", (uint64_t)*input.required_height_locktime);

    auto push_preimages = [&in](const char* name, const std::map<std::vector<unsigned char>, std::vector<unsigned char>>& map) {
        if (map.empty()) return;
        UniValue preimages(UniValue::VOBJ);
        for (const auto& entry : map) {
            preimages.pushKV(HexStr(entry.first), HexStr(entry.second));
        }
        in.pushKV(name, preimages);
    };
    push_preimages("ripemd160_preimages", input.ripemd160_preimages);
    push_preimages("sha256_preimages", input.sha256_preimages);
    push_preimages("hash160_preimages", input.hash160_preimages);
    push_preimages("hash256_preimages", input.hash256_preimages);

    if (!input.unknown.empty()) {
        UniValue unknowns(UniValue::VOBJ);
        for (auto entry : input.unknown) {
            unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
        }
        in.pushKV("unknown", unknowns);
    }
}

static void PsttOutputToUniv(const PSTTOutput& output, UniValue& out)
{
    if (output.amount) out.pushKV("amount_raw", *output.amount);
    if (!output.script.empty()) {
        UniValue s(UniValue::VOBJ);
        ScriptPubKeyToUniv(output.script, s, true);
        out.pushKV("script", s);
        if (output.amount) PushTokenAmount(output.script, *output.amount, out);
    }

    if (!output.redeem_script.empty()) {
        UniValue r(UniValue::VOBJ);
        ScriptToUniv(output.redeem_script, r, false);
        out.pushKV("redeem_script", r);
    }

    if (!output.hd_keypaths.empty()) {
        UniValue keypaths(UniValue::VARR);
        for (auto entry : output.hd_keypaths) {
            UniValue keypath(UniValue::VOBJ);
            keypath.pushKV("pubkey", HexStr(entry.first));
            uint32_t fingerprint = entry.second.at(0);
            keypath.pushKV("master_fingerprint", strprintf("%08x", internal_bswap_32(fingerprint)));
            entry.second.erase(entry.second.begin());
            keypath.pushKV("path", WriteHDKeypath(entry.second));
            keypaths.push_back(keypath);
        }
        out.pushKV("bip32_derivs", keypaths);
    }

    if (!output.unknown.empty()) {
        UniValue unknowns(UniValue::VOBJ);
        for (auto entry : output.unknown) {
            unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
        }
        out.pushKV("unknown", unknowns);
    }
}

// Parses a single {"address": amount} or {"data": hex} entry -- the same
// shape as one key-value pair of createrawtransaction's outputs array/dict --
// into a PSTTOutput. Shared by addoutputtopstt and addinputoutputpairtopstt,
// which each add exactly one output at a time rather than a whole array.
static PSTTOutput ParsePsttOutputEntry(const UniValue& output_in)
{
    if (!output_in.isObject() || output_in.size() != 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, output must be an object with exactly one key");
    }
    const std::string& name = output_in.getKeys()[0];
    const UniValue& value = output_in.getValues()[0];

    PSTTOutput out;
    if (name == "data") {
        std::vector<unsigned char> data = ParseHexV(value.getValStr(), "Data");
        out.amount = 0;
        out.script = CScript() << OP_RETURN << data;
        return out;
    }

    CTxDestination destination = DecodeDestination(name);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Tapyrus address: ") + name);
    }
    CTxOut txout = BuildDestinationTxOut(destination, value);
    out.amount = txout.nValue;
    out.script = txout.scriptPubKey;
    return out;
}

UniValue createpstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 6)
        throw std::runtime_error(
            "createpstt [{\"previous_txid\":\"id\",\"output_index\":n},...] [{\"address\":amount},{\"data\":\"hex\"},...] ( fallback_locktime ) ( inputs_modifiable ) ( outputs_modifiable ) ( has_sighash_single )\n"
            "\nCreates a bare PSTT from the given inputs and outputs (either may be empty).\n"
            "Implements the Creator role.\n"
            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"previous_txid\":\"id\", (string, required) The transaction id\n"
            "         \"output_index\":n,     (numeric, required) The output number\n"
            "         \"sequence\":n          (numeric, optional) The sequence number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"               (array, required) a json array with outputs (key-value pairs)\n"
            "   [\n"
            "    {\n"
            "      \"address\": x.xxx,    (obj, optional) A key-value pair. The key (string) is the tapyrus address, the value (float or string) is the amount in " + CURRENCY_UNIT + "\n"
            "    },\n"
            "    {\n"
            "      \"data\": \"hex\"        (obj, optional) A key-value pair. The key must be \"data\", the value is hex encoded data\n"
            "    }\n"
            "    ,...                     More key-value pairs of the above form. For compatibility reasons, a dictionary, which holds the key-value pairs directly, is also\n"
            "                             accepted as second parameter.\n"
            "   ]\n"
            "3. fallback_locktime         (numeric, optional, default=0) PSTT_GLOBAL_FALLBACK_LOCKTIME -- the locktime used when no input constrains one\n"
            "4. inputs_modifiable         (boolean, optional, default=false) Whether further inputs may be added via addinputtopstt/addinputoutputpairtopstt\n"
            "5. outputs_modifiable        (boolean, optional, default=false) Whether further outputs may be added via addoutputtopstt/addinputoutputpairtopstt\n"
            "6. has_sighash_single        (boolean, optional, default=false) Sets the Has-SIGHASH_SINGLE bit -- future Constructor calls must use addinputoutputpairtopstt instead of separate addinputtopstt/addoutputtopstt calls\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("createpstt", "\"[{\\\"previous_txid\\\":\\\"myid\\\",\\\"output_index\\\":0}]\" \"[{\\\"myaddress\\\":0.01}]\"")
        );

    RPCTypeCheck(request.params, {
        UniValue::VARR,
        UniValueType(), // ARR or OBJ, checked later
        UniValue::VNUM,
        UniValue::VBOOL,
        UniValue::VBOOL,
        UniValue::VBOOL,
        }, true
    );

    // Passing fallback_locktime through as ConstructTransaction's own locktime
    // param (rather than NullUniValue) is deliberate, not just convenient
    // range-check reuse: it's what makes ConstructTransaction apply its
    // existing nSequence = max-1 coupling to every input when the locktime is
    // nonzero. Without that, a nonzero fallback_locktime would be silently
    // inert once extracted -- IsFinalTx-style consensus rules only honor
    // nLockTime when at least one input carries a non-final sequence.
    // Inputs are parsed separately via ParsePsttInputEntries (PSTT's own
    // previous_txid/output_index field names) -- an empty array is passed
    // here so ConstructTransaction only handles outputs/locktime, not
    // createrawtransaction's txid/vout input shape.
    CMutableTransaction rawTx = ConstructTransaction(UniValue(UniValue::VARR), request.params[1], request.params[2], NullUniValue);
    rawTx.vin = ParsePsttInputEntries(request.params[0], rawTx.nLockTime, /*rbfOptIn=*/false);

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = CTransaction::CURRENT_FEATURES;
    for (const CTxIn& txin : rawTx.vin) {
        PSTTInput input;
        input.previous_txid = txin.prevout.hashMalFix;
        input.prev_out_index = txin.prevout.n;
        input.previous_txid_set = true;
        input.prev_out_index_set = true;
        if (txin.nSequence != CTxIn::SEQUENCE_FINAL) input.sequence = txin.nSequence;
        pstt.inputs.push_back(std::move(input));
    }
    for (const CTxOut& txout : rawTx.vout) {
        PSTTOutput output;
        output.amount = txout.nValue;
        output.script = txout.scriptPubKey;
        pstt.outputs.push_back(std::move(output));
    }

    if (rawTx.nLockTime != 0) pstt.fallback_locktime = rawTx.nLockTime;

    bool inputs_modifiable = !request.params[3].isNull() && request.params[3].get_bool();
    bool outputs_modifiable = !request.params[4].isNull() && request.params[4].get_bool();
    bool has_sighash_single = !request.params[5].isNull() && request.params[5].get_bool();
    uint8_t modifiable = 0;
    if (inputs_modifiable) modifiable |= PSTT_TXMOD_INPUTS_MODIFIABLE;
    if (outputs_modifiable) modifiable |= PSTT_TXMOD_OUTPUTS_MODIFIABLE;
    if (has_sighash_single) modifiable |= PSTT_TXMOD_HAS_SIGHASH_SINGLE;
    if (modifiable != 0) pstt.tx_modifiable = modifiable;

    return EncodePSTT(pstt);
}

UniValue converttopstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "converttopstt \"hexstring\" ( permitsigdata inputs_modifiable outputs_modifiable )\n"
            "\nConverts a network serialized transaction to a PSTT. This should be used only with createrawtransaction and fundrawtransaction.\n"
            "createpstt and walletcreatefundedpstt should be used for new applications. Implements the Creator role.\n"
            "\nArguments:\n"
            "1. \"hexstring\"              (string, required) The hex string of a raw transaction\n"
            "2. permitsigdata           (boolean, optional, default=false) If true, any scriptSigs in the inputs are discarded and\n"
            "                             conversion continues. If false, RPC fails if any scriptSig is present.\n"
            "3. inputs_modifiable       (boolean, optional, default=false) Whether further inputs may be added via addinputtopstt/addinputoutputpairtopstt\n"
            "4. outputs_modifiable      (boolean, optional, default=false) Whether further outputs may be added via addoutputtopstt/addinputoutputpairtopstt\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"[{\\\"data\\\":\\\"00010203\\\"}]\"") +
            "\nConvert the transaction to a PSTT\n"
            + HelpExampleCli("converttopstt", "\"rawtransaction\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL, UniValue::VBOOL, UniValue::VBOOL}, true);

    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    }

    bool permitsigdata = !request.params[1].isNull() && request.params[1].get_bool();
    for (CTxIn& input : tx.vin) {
        if (!input.scriptSig.empty() && !permitsigdata) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Inputs must not have scriptSigs");
        }
        input.scriptSig.clear();
    }

    PartiallySignedTapyrusTransaction pstt;
    pstt.tx_features = tx.nFeatures;
    for (const CTxIn& txin : tx.vin) {
        PSTTInput input;
        input.previous_txid = txin.prevout.hashMalFix;
        input.prev_out_index = txin.prevout.n;
        input.previous_txid_set = true;
        input.prev_out_index_set = true;
        if (txin.nSequence != CTxIn::SEQUENCE_FINAL) input.sequence = txin.nSequence;
        pstt.inputs.push_back(std::move(input));
    }
    for (const CTxOut& txout : tx.vout) {
        PSTTOutput output;
        output.amount = txout.nValue;
        output.script = txout.scriptPubKey;
        pstt.outputs.push_back(std::move(output));
    }
    if (tx.nLockTime != 0) pstt.fallback_locktime = tx.nLockTime;

    bool inputs_modifiable = !request.params[2].isNull() && request.params[2].get_bool();
    bool outputs_modifiable = !request.params[3].isNull() && request.params[3].get_bool();
    uint8_t modifiable = 0;
    if (inputs_modifiable) modifiable |= PSTT_TXMOD_INPUTS_MODIFIABLE;
    if (outputs_modifiable) modifiable |= PSTT_TXMOD_OUTPUTS_MODIFIABLE;
    if (modifiable != 0) pstt.tx_modifiable = modifiable;

    return EncodePSTT(pstt);
}

UniValue addinputtopstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "addinputtopstt \"pstt\" \"previous_txid\" output_index ( sequence )\n"
            "\nAppends one input to a PSTT whose Inputs-Modifiable flag is set. Implements\n"
            "the Constructor role. Refuses if the PSTT's Has-SIGHASH_SINGLE flag is set --\n"
            "use addinputoutputpairtopstt instead in that case, so an input is never added\n"
            "without its paired output in the same call.\n"
            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"previous_txid\"        (string, required) The transaction id\n"
            "3. output_index           (numeric, required) The output number\n"
            "4. sequence               (numeric, optional) The sequence number\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("addinputtopstt", "\"pstt\" \"mytxid\" 0")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VNUM, UniValue::VNUM}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    if (!pstt.tx_modifiable || !(*pstt.tx_modifiable & PSTT_TXMOD_INPUTS_MODIFIABLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT inputs are not modifiable");
    }
    if (pstt.tx_modifiable && (*pstt.tx_modifiable & PSTT_TXMOD_HAS_SIGHASH_SINGLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "PSTT has a SIGHASH_SINGLE signature; use addinputoutputpairtopstt to add an input and output together");
    }

    PSTTInput input;
    input.previous_txid = ParseHashV(request.params[1], "previous_txid");
    int nOutput = request.params[2].get_int();
    if (nOutput < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, output_index must be positive");
    }
    input.prev_out_index = (uint32_t)nOutput;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    if (!request.params[3].isNull()) {
        int64_t seq = request.params[3].get_int64();
        if (seq < 0 || seq > std::numeric_limits<uint32_t>::max()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
        }
        input.sequence = (uint32_t)seq;
    }
    pstt.inputs.push_back(std::move(input));

    uint32_t locktime;
    if (!ComputeLocktime(pstt, locktime)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Adding this input would make the PSTT's locktime unsatisfiable");
    }

    return EncodePSTT(pstt);
}

UniValue addoutputtopstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "addoutputtopstt \"pstt\" output\n"
            "\nAppends one output to a PSTT whose Outputs-Modifiable flag is set. Implements\n"
            "the Constructor role. Refuses if the PSTT's Has-SIGHASH_SINGLE flag is set --\n"
            "use addinputoutputpairtopstt instead in that case.\n"
            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"output\"               (object, required) A single key-value pair, either\n"
            "                          {\"address\": amount} or {\"data\": \"hex\"} -- same shape as\n"
            "                          one entry of createrawtransaction's outputs array\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("addoutputtopstt", "\"pstt\" \"{\\\"myaddress\\\":0.01}\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ});

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    if (!pstt.tx_modifiable || !(*pstt.tx_modifiable & PSTT_TXMOD_OUTPUTS_MODIFIABLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT outputs are not modifiable");
    }
    if (pstt.tx_modifiable && (*pstt.tx_modifiable & PSTT_TXMOD_HAS_SIGHASH_SINGLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "PSTT has a SIGHASH_SINGLE signature; use addinputoutputpairtopstt to add an input and output together");
    }

    pstt.outputs.push_back(ParsePsttOutputEntry(request.params[1]));
    return EncodePSTT(pstt);
}

UniValue addinputoutputpairtopstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 4 || request.params.size() > 5)
        throw std::runtime_error(
            "addinputoutputpairtopstt \"pstt\" \"previous_txid\" output_index output ( sequence )\n"
            "\nAtomically appends one input and one output to a PSTT in a single re-encode.\n"
            "Implements the Constructor role. Required (instead of separate\n"
            "addinputtopstt/addoutputtopstt calls) whenever the PSTT's Has-SIGHASH_SINGLE\n"
            "flag is set, so a client crash between two separate calls can never leave the\n"
            "PSTT with a transiently unpaired input.\n"
            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"previous_txid\"        (string, required) The transaction id\n"
            "3. output_index           (numeric, required) The output number\n"
            "4. \"output\"               (object, required) A single key-value pair, either\n"
            "                          {\"address\": amount} or {\"data\": \"hex\"}\n"
            "5. sequence               (numeric, optional) The sequence number\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("addinputoutputpairtopstt", "\"pstt\" \"mytxid\" 0 \"{\\\"myaddress\\\":0.01}\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VSTR, UniValue::VNUM, UniValue::VOBJ, UniValue::VNUM}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    if (!pstt.tx_modifiable || !(*pstt.tx_modifiable & PSTT_TXMOD_INPUTS_MODIFIABLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT inputs are not modifiable");
    }
    if (!pstt.tx_modifiable || !(*pstt.tx_modifiable & PSTT_TXMOD_OUTPUTS_MODIFIABLE)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT outputs are not modifiable");
    }

    PSTTInput input;
    input.previous_txid = ParseHashV(request.params[1], "previous_txid");
    int nOutput = request.params[2].get_int();
    if (nOutput < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, output_index must be positive");
    }
    input.prev_out_index = (uint32_t)nOutput;
    input.previous_txid_set = true;
    input.prev_out_index_set = true;
    if (!request.params[4].isNull()) {
        int64_t seq = request.params[4].get_int64();
        if (seq < 0 || seq > std::numeric_limits<uint32_t>::max()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
        }
        input.sequence = (uint32_t)seq;
    }

    pstt.inputs.push_back(std::move(input));
    pstt.outputs.push_back(ParsePsttOutputEntry(request.params[3]));

    uint32_t locktime;
    if (!ComputeLocktime(pstt, locktime)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Adding this input would make the PSTT's locktime unsatisfiable");
    }

    return EncodePSTT(pstt);
}

UniValue finalizepsttconstruction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "finalizepsttconstruction \"pstt\" ( clear_inputs_modifiable clear_outputs_modifiable )\n"
            "\nDeclares a PSTT's Constructor phase finished by clearing its Inputs-Modifiable\n"
            "and/or Outputs-Modifiable flags. Implements the finishing half of the\n"
            "Constructor role.\n"
            "\nArguments:\n"
            "1. \"pstt\"                       (string, required) A base64 string of a PSTT\n"
            "2. clear_inputs_modifiable      (boolean, optional, default=true) Clear the Inputs-Modifiable flag\n"
            "3. clear_outputs_modifiable     (boolean, optional, default=true) Clear the Outputs-Modifiable flag\n"
            "\nResult:\n"
            "  \"pstt\"        (string)  The resulting PSTT (base64-encoded string)\n"
            "\nExamples:\n"
            + HelpExampleCli("finalizepsttconstruction", "\"pstt\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL, UniValue::VBOOL}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    bool clear_inputs = request.params[1].isNull() || request.params[1].get_bool();
    bool clear_outputs = request.params[2].isNull() || request.params[2].get_bool();

    uint8_t modifiable = pstt.tx_modifiable.value_or(0);
    if (clear_inputs) modifiable &= ~PSTT_TXMOD_INPUTS_MODIFIABLE;
    if (clear_outputs) modifiable &= ~PSTT_TXMOD_OUTPUTS_MODIFIABLE;
    pstt.tx_modifiable = modifiable;

    return EncodePSTT(pstt);
}

UniValue decodepstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodepstt \"pstt\"\n"
            "\nReturn a JSON object representing the serialized, base64-encoded "
            "Partially Signed Tapyrus Transaction (see doc/tapyrus/pstt.md).\n"

            "\nArguments:\n"
            "1. \"pstt\"            (string, required) The PSTT base64 string\n"

            "\nExamples:\n"
            + HelpExampleCli("decodepstt", "\"pstt\"")
    );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    UniValue result(UniValue::VOBJ);

    if (pstt.tx_features) result.pushKV("tx_features", *pstt.tx_features);
    if (pstt.fallback_locktime) result.pushKV("fallback_locktime", (uint64_t)*pstt.fallback_locktime);
    {
        // Report the effective bitfield even when the field is absent from
        // the wire -- absent and present-with-value-0 both mean "not
        // modifiable" (see doc/tapyrus/pstt.md's PSTT_GLOBAL_TX_MODIFIABLE
        // row), so decodepstt's diagnostic output shouldn't vary with which
        // of those two equivalent encodings a given PSTT happens to use.
        uint8_t modifiable = pstt.tx_modifiable.value_or(0);
        UniValue mod(UniValue::VOBJ);
        mod.pushKV("inputs_modifiable", bool(modifiable & PSTT_TXMOD_INPUTS_MODIFIABLE));
        mod.pushKV("outputs_modifiable", bool(modifiable & PSTT_TXMOD_OUTPUTS_MODIFIABLE));
        mod.pushKV("has_sighash_single", bool(modifiable & PSTT_TXMOD_HAS_SIGHASH_SINGLE));
        result.pushKV("tx_modifiable", mod);
    }
    if (pstt.version) result.pushKV("version", (uint64_t)*pstt.version);

    if (!pstt.xpubs.empty()) {
        UniValue xpubs(UniValue::VARR);
        for (const auto& entry : pstt.xpubs) {
            UniValue x(UniValue::VOBJ);
            x.pushKV("xpub", HexStr(SerializeXpubKeyData(entry.first)));
            std::vector<uint32_t> path = entry.second;
            if (!path.empty()) {
                uint32_t fingerprint = path.at(0);
                x.pushKV("master_fingerprint", strprintf("%08x", internal_bswap_32(fingerprint)));
                path.erase(path.begin());
                x.pushKV("path", WriteHDKeypath(path));
            }
            xpubs.push_back(x);
        }
        result.pushKV("xpubs", xpubs);
    }

    uint32_t locktime;
    if (ComputeLocktime(pstt, locktime)) {
        result.pushKV("locktime", (uint64_t)locktime);
    }
    try {
        result.pushKV("identification_txid", pstt.GetIdentifier().GetHex());
    } catch (const std::exception&) {
        // No valid locktime yet (contradictory required locktimes) -- the
        // identifier can't be computed; omit rather than error, since
        // decodepstt is an inspection tool and this is exactly the kind of
        // malformed-but-parseable PSTT it should still be able to show.
    }

    if (!pstt.unknown.empty()) {
        UniValue unknowns(UniValue::VOBJ);
        for (auto entry : pstt.unknown) {
            unknowns.pushKV(HexStr(entry.first), HexStr(entry.second));
        }
        result.pushKV("unknown", unknowns);
    }

    UniValue inputs(UniValue::VARR);
    for (const PSTTInput& input : pstt.inputs) {
        UniValue in(UniValue::VOBJ);
        PsttInputToUniv(input, in);
        inputs.push_back(in);
    }
    result.pushKV("inputs", inputs);

    UniValue outputs(UniValue::VARR);
    for (const PSTTOutput& output : pstt.outputs) {
        UniValue out(UniValue::VOBJ);
        PsttOutputToUniv(output, out);
        outputs.push_back(out);
    }
    result.pushKV("outputs", outputs);

    return result;
}

UniValue combinepstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "combinepstt [\"pstt\",...]\n"
            "\nCombine multiple partially signed Tapyrus transactions that all refer to\n"
            "the same underlying transaction into one. Implements the Combiner role.\n"

            "\nArguments:\n"
            "1. \"txs\"                   (string) A json array of base64 strings of partially signed transactions\n"
            "    [\n"
            "      \"pstt\"             (string) A base64 string of a PSTT\n"
            "      ,...\n"
            "    ]\n"

            "\nResult:\n"
            "  \"pstt\"          (string) The base64-encoded combined partially signed transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("combinepstt", "[\"mybase64_1\", \"mybase64_2\", \"mybase64_3\"]")
        );

    RPCTypeCheck(request.params, {UniValue::VARR}, true);

    std::vector<PartiallySignedTapyrusTransaction> psttxs;
    UniValue txs = request.params[0].get_array();
    for (unsigned int i = 0; i < txs.size(); ++i) {
        PartiallySignedTapyrusTransaction pstt;
        std::string error;
        if (!DecodePSTT(pstt, txs[i].get_str(), error)) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
        }
        psttxs.push_back(pstt);
    }

    if (psttxs.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Parameter 'txs' cannot be empty");
    }

    PartiallySignedTapyrusTransaction merged_pstt(psttxs[0]);
    for (auto it = std::next(psttxs.begin()); it != psttxs.end(); ++it) {
        if (!merged_pstt.HasSameIdentifierAs(*it)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTTs do not refer to the same transaction.");
        }
        try {
            merged_pstt.Merge(*it);
        } catch (const std::exception& e) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Cannot combine PSTTs: %s", e.what()));
        }
    }
    if (!merged_pstt.IsSane()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Merged PSTT is inconsistent");
    }

    return EncodePSTT(merged_pstt);
}

// The dummy provider's per-input sighash/scheme is derived from what the
// input itself already carries, not hardcoded: this makes SignPSTTInput's
// sighash-conflict and scheme-conflict pre-checks unreachable here by
// construction (they can only ever fire against a mismatch with what we
// pass in), so finalization only ever fails for a genuine structural reason
// (missing/mismatched UTXO, bad redeem script, contradictory locktime, an
// out-of-range SIGHASH_SINGLE), never a spurious conflict against our own
// derivation.
static int FinalizeSighashFor(const PSTTInput& input)
{
    return input.sighash_type ? *input.sighash_type : SIGHASH_ALL;
}

static SignatureScheme FinalizeSigSchemeFor(const PSTTInput& input)
{
    if (input.partial_sigs.empty()) return SignatureScheme::ECDSA;
    const std::vector<unsigned char>& sig = input.partial_sigs.begin()->second.second;
    return sig.size() == CPubKey::COMPACT_SIGNATURE_SIZE ? SignatureScheme::SCHNORR : SignatureScheme::ECDSA;
}

UniValue finalizepstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "finalizepstt \"pstt\" ( extract )\n"
            "\nFinalize the inputs of a PSTT. If every input is fully signed, produces a\n"
            "network-serialized transaction that can be broadcast with sendrawtransaction.\n"
            "Otherwise returns a PSTT with PSTT_IN_FINAL_SCRIPTSIG filled in for the inputs\n"
            "that are complete. Implements the Finalizer and (optionally) Extractor roles.\n"

            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"
            "2. \"extract\"              (boolean, optional, default=true) If true and the transaction is complete,\n"
            "                             extract and return the complete transaction in normal network serialization\n"
            "                             instead of the PSTT.\n"

            "\nResult:\n"
            "{\n"
            "  \"pstt\" : \"value\",          (string) The base64-encoded partially signed transaction if not extracted\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded network transaction if extracted\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("finalizepstt", "\"pstt\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VBOOL}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    if (pstt.tx_modifiable && (*pstt.tx_modifiable & (PSTT_TXMOD_INPUTS_MODIFIABLE | PSTT_TXMOD_OUTPUTS_MODIFIABLE)) != 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "PSTT construction is not finished (inputs and/or outputs are still modifiable)");
    }

    bool complete = true;
    for (unsigned int i = 0; i < pstt.inputs.size(); ++i) {
        PSTTInput& input = pstt.inputs.at(i);
        if (!input.final_script_sig.empty()) continue; // already finalized

        SignatureData sigdata;
        PSTTSignResult result = SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, i, sigdata,
                                               FinalizeSighashFor(input), FinalizeSigSchemeFor(input));
        if (result == PSTTSignResult::MISSING_UTXO) {
            complete = false;
            continue;
        }
        if (result != PSTTSignResult::OK) {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, strprintf("Finalizing input %d failed: %s", i, PSTTSignResultToString(result)));
        }

        input.FromSignatureData(sigdata);
        if (!sigdata.complete) {
            complete = false;
            continue;
        }

        // Finalizer field policy: keep required fields, PSTT_IN_UTXO and
        // unknowns; strip everything only useful during signing.
        input.partial_sigs.clear();
        input.sighash_type = boost::none;
        input.redeem_script.clear();
        input.hd_keypaths.clear();
        input.ripemd160_preimages.clear();
        input.sha256_preimages.clear();
        input.hash160_preimages.clear();
        input.hash256_preimages.clear();
    }

    UniValue result(UniValue::VOBJ);
    bool extract = request.params[1].isNull() || request.params[1].get_bool();
    if (complete && extract) {
        CMutableTransaction mtx = ExtractPSTT(pstt);
        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << mtx;
        result.pushKV("hex", HexStr(ssTx.begin(), ssTx.end()));
    } else {
        result.pushKV("pstt", EncodePSTT(pstt));
    }
    result.pushKV("complete", complete);
    return result;
}

UniValue extractpstt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "extractpstt \"pstt\"\n"
            "\nExtract the fully signed transaction from a PSTT and return it in network\n"
            "serialization, ready to be broadcast with sendrawtransaction. Implements the\n"
            "Transaction Extractor role. Throws if any input is missing PSTT_IN_FINAL_SCRIPTSIG.\n"

            "\nArguments:\n"
            "1. \"pstt\"                 (string, required) A base64 string of a PSTT\n"

            "\nResult:\n"
            "  \"hex\"              (string) The hex-encoded network transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("extractpstt", "\"pstt\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR});

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    CMutableTransaction mtx;
    try {
        mtx = ExtractPSTT(pstt);
    } catch (const std::runtime_error& e) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, e.what());
    }

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << mtx;
    return HexStr(ssTx.begin(), ssTx.end());
}

UniValue signpsttwithkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "signpsttwithkey \"pstt\" [\"privatekey1\",...] ( \"sighashtype\" \"sigscheme\" )\n"
            "\nSign inputs of a PSTT with the given private keys (Signer role, no wallet required).\n"

            "\nArguments:\n"
            "1. \"pstt\"                          (string, required) The PSTT base64 string\n"
            "2. \"privkeys\"                      (string, required) A json array of private keys for signing\n"
            "    [\n"
            "      \"privatekey\"                  (string) private key in WIF (Wallet Import Format, see dumpprivkey)\n"
            "      ,...\n"
            "    ]\n"
            "3. \"sighashtype\"                    (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"
            "4. \"sigscheme\"                    (string, optional, default=ECDSA) The signature scheme to use\n"
            "       \"ECDSA\"\n"
            "       \"SCHNORR\"\n"

            "\nResult:\n"
            "{\n"
            "  \"pstt\" : \"value\",          (string) The base64-encoded partially signed transaction\n"
            "  \"complete\" : true|false,   (boolean) If every input with a UTXO now has a complete signature\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signpsttwithkey", "\"mypstt\" \"[\\\"key1\\\",\\\"key2\\\"]\"")
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR, UniValue::VSTR, UniValue::VSTR}, true);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, request.params[0].get_str(), error)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, strprintf("TX decode failed %s", error));
    }

    FlatSigningProvider provider;
    const UniValue& keys = request.params[1].get_array();
    for (unsigned int idx = 0; idx < keys.size(); ++idx) {
        UniValue k = keys[idx];
        CKey key = DecodeSecret(k.get_str());
        if (!key.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
        }
        CPubKey pubkey = key.GetPubKey();
        provider.keys[pubkey.GetID()] = key;
        provider.pubkeys[pubkey.GetID()] = pubkey;
    }
    // Redeem scripts already attached to inputs by an earlier Updater step
    // are made available for P2SH/CP2SH resolution.
    for (const PSTTInput& in : pstt.inputs) {
        if (!in.redeem_script.empty()) {
            provider.scripts[CScriptID(in.redeem_script)] = in.redeem_script;
        }
    }

    int sighash = ParseSighashString(request.params[2]);
    SignatureScheme sigScheme = ParseSigSchemeString(request.params[3]);

    bool complete = true;
    for (unsigned int i = 0; i < pstt.inputs.size(); ++i) {
        PSTTInput& input = pstt.inputs.at(i);
        if (!input.final_script_sig.empty()) continue; // already finalized
        SignatureData sigdata;
        PSTTSignResult result = SignPSTTInput(provider, pstt, i, sigdata, sighash, sigScheme);
        if (result == PSTTSignResult::MISSING_UTXO) {
            // Not this call's job to complain -- an incremental multi-party
            // PSTT may genuinely have inputs no one has attached a UTXO to
            // yet; leave it for a later Updater/Signer round.
            complete = false;
            continue;
        }
        if (result != PSTTSignResult::OK) {
            throw JSONRPCError(RPC_TRANSACTION_ERROR, strprintf("Signing input %d failed: %s", i, PSTTSignResultToString(result)));
        }
        input.FromSignatureData(sigdata);
        if (!sigdata.complete) complete = false;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("pstt", EncodePSTT(pstt));
    result.pushKV("complete", complete);
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                            actor (function)            argNames
  //  --------------------- ------------------------        -----------------------     ----------
    { "rawtransactions",    "getrawtransaction",            &getrawtransaction,         {"txid","verbose","blockhash"} },
    { "rawtransactions",    "createrawtransaction",         &createrawtransaction,      {"inputs","outputs","locktime","replaceable"} },
    { "rawtransactions",    "decoderawtransaction",         &decoderawtransaction,      {"hexstring"} },
    { "rawtransactions",    "decodescript",                 &decodescript,              {"hexstring"} },
    { "rawtransactions",    "sendrawtransaction",           &sendrawtransaction,        {"hexstring","allowhighfees"} },
    { "rawtransactions",    "combinerawtransaction",        &combinerawtransaction,     {"txs"} },
    { "rawtransactions",    "signrawtransaction",           &signrawtransaction,        {"hexstring","prevtxs","privkeys","sighashtype"} }, /* uses wallet if enabled */
    { "rawtransactions",    "signrawtransactionwithkey",    &signrawtransactionwithkey, {"hexstring","privkeys","prevtxs","sighashtype"} },

    { "rawtransactions",    "createpstt",                   &createpstt,                {"inputs","outputs","fallback_locktime","inputs_modifiable","outputs_modifiable","has_sighash_single"} },
    { "rawtransactions",    "converttopstt",                &converttopstt,             {"hexstring","permitsigdata","inputs_modifiable","outputs_modifiable"} },
    { "rawtransactions",    "addinputtopstt",               &addinputtopstt,            {"pstt","previous_txid","output_index","sequence"} },
    { "rawtransactions",    "addoutputtopstt",              &addoutputtopstt,           {"pstt","output"} },
    { "rawtransactions",    "addinputoutputpairtopstt",     &addinputoutputpairtopstt,  {"pstt","previous_txid","output_index","output","sequence"} },
    { "rawtransactions",    "finalizepsttconstruction",     &finalizepsttconstruction,  {"pstt","clear_inputs_modifiable","clear_outputs_modifiable"} },
    { "rawtransactions",    "decodepstt",                   &decodepstt,                {"pstt"} },
    { "rawtransactions",    "combinepstt",                  &combinepstt,               {"txs"} },
    { "rawtransactions",    "finalizepstt",                 &finalizepstt,              {"pstt", "extract"} },
    { "rawtransactions",    "extractpstt",                  &extractpstt,               {"pstt"} },
    { "rawtransactions",    "signpsttwithkey",               &signpsttwithkey,           {"pstt","privkeys","sighashtype","sigscheme"} },

    { "blockchain",         "gettxoutproof",                &gettxoutproof,             {"txids", "blockhash"} },
    { "blockchain",         "verifytxoutproof",             &verifytxoutproof,          {"proof"} },
};

void RegisterRawTransactionRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
