// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cs_main.h>
#include <chain.h>
#include <coin.h>
#include <coinsview.h>
#include <core_io.h>
#include <key_io.h>
#include <validation.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <rpc/jsonrpcrequest.h>
#include <rpc/rpctable.h>
#include <script/descriptor.h>
#include <shutdown.h>
#include <streams.h>
#include <sync.h>
#include <txdb.h>
#include <util.h>
#include <utilstrencodings.h>
#include <utxo_snapshot.h>
#include <hash.h>

#include <assert.h>
#include <stdexcept>
#include <stdint.h>

#include <univalue.h>

#include <memory>
#include <mutex>

struct CCoinsStats
{
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nBogoSize;
    uint256 hashSerialized;
    uint256 hashIssuedColorIds;
    uint64_t nDiskSize;
    TxColoredCoinBalancesMap mTotalAmount;

    CCoinsStats() : nHeight(0), nTransactions(0), nTransactionOutputs(0), nBogoSize(0), nDiskSize(0){ mTotalAmount[ColorIdentifier()] = 0; }
};

static void ApplyStats(CCoinsStats &stats, CHashWriter& ss, const uint256& hash, const std::map<uint32_t, Coin>& outputs)
{
    assert(!outputs.empty());
    ss << hash;
    ss << VARINT(outputs.begin()->second.nHeight * 2 + (outputs.begin()->second.fCoinBase ? 1u : 0u));
    stats.nTransactions++;
    for (const auto& output : outputs) {
        ss << VARINT(output.first + 1);
        ss << output.second.out.scriptPubKey;
        ss << VARINT_MODE(output.second.out.nValue, VarIntMode::NONNEGATIVE_SIGNED);
        stats.nTransactionOutputs++;
        stats.mTotalAmount[GetColorIdFromScript(output.second.out.scriptPubKey)] += output.second.out.nValue;
        stats.nBogoSize += 32 /* txid */ + 4 /* vout index */ + 4 /* height + coinbase */ + 8 /* amount */ +
                           2 /* scriptPubKey len */ + output.second.out.scriptPubKey.size() /* scriptPubKey */;
    }
    ss << VARINT(0u);
}

//! Calculate statistics about the unspent transaction output set
static bool GetUTXOStats(CCoinsViewDB *view, CCoinsStats &stats)
{
    std::unique_ptr<CCoinsViewDBCursor> pcursor(static_cast<CCoinsViewDBCursor*>(view->Cursor()));
    assert(pcursor);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetBestBlock();
    {
        LOCK(cs_main);
        stats.nHeight = LookupBlockIndex(stats.hashBlock)->nHeight;
    }
    ss << stats.hashBlock;
    uint256 prevkey;
    std::map<uint32_t, Coin> outputs;
    while (pcursor->Valid()) {
        COutPoint key;
        Coin coin;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            if (!outputs.empty() && key.hashMalFix != prevkey) {
                ApplyStats(stats, ss, prevkey, outputs);
                outputs.clear();
            }
            prevkey = key.hashMalFix;
            outputs.emplace(key.n, coin);
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    if (!outputs.empty()) {
        ApplyStats(stats, ss, prevkey, outputs);
    }
    stats.hashSerialized = ss.GetHash();
    // Issued NON_REISSUABLE/NFT colorIds get their own hash so that
    // hash_serialized_3 keeps describing the coins alone. Read from the same
    // database snapshot as the coins; keys come back in stored order.
    CHashWriter ss_colorids(SER_GETHASH, PROTOCOL_VERSION);
    std::vector<unsigned char> colorId;
    for (pcursor->SeekIssuedColorIds(); pcursor->GetIssuedColorId(colorId); pcursor->NextIssuedColorId()) {
        ss_colorids << colorId;
    }
    stats.hashIssuedColorIds = ss_colorids.GetHash();
    stats.nDiskSize = view->EstimateSize();
    return true;
}

static UniValue gettxoutsetinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set.\n"
            "Note this call may take some time.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) The hash of the block at the tip of the chain\n"
            "  \"transactions\": n,      (numeric) The number of transactions with unspent outputs\n"
            "  \"txouts\": n,            (numeric) The number of unspent transaction outputs\n"
            "  \"bogosize\": n,          (numeric) A meaningless metric for UTXO set size\n"
            "  \"hash_serialized_3\": \"hash\", (string) The serialized hash of the UTXO set\n"
            "  \"issued_colorids_hash\": \"hash\", (string) The hash of the issued NON_REISSUABLE and NFT colorIds\n"
            "  \"disk_size\": n,         (numeric) The estimated size of the chainstate on disk\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (GetUTXOStats(pcoinsdbview.get(), stats)) {
        ret.pushKV("height", (int64_t)stats.nHeight);
        ret.pushKV("bestblock", stats.hashBlock.GetHex());
        ret.pushKV("transactions", (int64_t)stats.nTransactions);
        ret.pushKV("txouts", (int64_t)stats.nTransactionOutputs);
        ret.pushKV("bogosize", (int64_t)stats.nBogoSize);
        ret.pushKV("hash_serialized_3", stats.hashSerialized.GetHex());
        ret.pushKV("issued_colorids_hash", stats.hashIssuedColorIds.GetHex());
        ret.pushKV("disk_size", stats.nDiskSize);

        UniValue amount(UniValue::VOBJ);
        for(auto amountPair:stats.mTotalAmount)
            amount.pushKV(amountPair.first.toHexString(), amountPair.first.type == TokenTypes::NONE ? ValueFromAmount(amountPair.second) : amountPair.second );
        ret.pushKV("total_amount", amount);
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
    }
    return ret;
}

//! Search for a given set of pubkey scripts
bool FindScriptPubKey(std::atomic<int>& scan_progress, const std::atomic<bool>& should_abort, int64_t& count, CCoinsViewCursor* cursor, const std::set<CScript>& needles, std::map<COutPoint, Coin>& out_results) {
    scan_progress = 0;
    count = 0;
    while (cursor->Valid()) {
        COutPoint key;
        Coin coin;
        if (!cursor->GetKey(key) || !cursor->GetValue(coin)) return false;
        if (++count % 8192 == 0) {
            if (should_abort) {
                // allow to abort the scan via the abort reference
                return false;
            }
        }
        if (count % 256 == 0) {
            // update progress reference every 256 item
            uint32_t high = 0x100 * *key.hashMalFix.begin() + *(key.hashMalFix.begin() + 1);
            scan_progress = (int)(high * 100.0 / 65536.0 + 0.5);
        }
        if (needles.count(coin.out.scriptPubKey)) {
            out_results.emplace(key, coin);
        }
        cursor->Next();
    }
    scan_progress = 100;
    return true;
}

/** RAII object to prevent concurrency issue when scanning the txout set */
static std::mutex g_utxosetscan;
static std::atomic<int> g_scan_progress;
static std::atomic<bool> g_scan_in_progress;
static std::atomic<bool> g_should_abort_scan;
class CoinsViewScanReserver
{
private:
    bool m_could_reserve;
public:
    explicit CoinsViewScanReserver() : m_could_reserve(false) {}

    bool reserve() {
        assert (!m_could_reserve);
        std::lock_guard<std::mutex> lock(g_utxosetscan);
        if (g_scan_in_progress) {
            return false;
        }
        g_scan_in_progress = true;
        m_could_reserve = true;
        return true;
    }

    ~CoinsViewScanReserver() {
        if (m_could_reserve) {
            std::lock_guard<std::mutex> lock(g_utxosetscan);
            g_scan_in_progress = false;
        }
    }
};

UniValue scantxoutset(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "scantxoutset <action> ( <scanobjects> )\n"
            "\nEXPERIMENTAL warning: this call may be removed or changed in future releases.\n"
            "\nScans the unspent transaction output set for entries that match certain output descriptors.\n"
            "Examples of output descriptors are:\n"
            "    addr(<address>)                      Outputs whose scriptPubKey corresponds to the specified address (does not include P2PK)\n"
            "    raw(<hex script>)                    Outputs whose scriptPubKey equals the specified hex scripts\n"
            "    combo(<pubkey>)                      P2PK and P2PKH outputs for the given pubkey\n"
            "    pkh(<pubkey>)                        P2PKH outputs for the given pubkey\n"
            "    sh(multi(<n>,<pubkey>,<pubkey>,...)) P2SH-multisig outputs for the given threshold and pubkeys\n"
            "\nIn the above, <pubkey> either refers to a fixed public key in hexadecimal notation, or to an xpub/xprv optionally followed by one\n"
            "or more path elements separated by \"/\", and optionally ending in \"/*\" (unhardened), or \"/*'\" or \"/*h\" (hardened) to specify all\n"
            "unhardened or hardened child keys.\n"
            "In the latter case, a range needs to be specified by below if different from 1000.\n"
            "For more information on output descriptors, see the documentation in the doc/descriptors.md file.\n"
            "\nArguments:\n"
            "1. \"action\"                       (string, required) The action to execute\n"
            "                                      \"start\" for starting a scan\n"
            "                                      \"abort\" for aborting the current scan (returns true when abort was successful)\n"
            "                                      \"status\" for progress report (in %) of the current scan\n"
            "2. \"scanobjects\"                  (array, required) Array of scan objects\n"
            "    [                             Every scan object is either a string descriptor or an object:\n"
            "        \"descriptor\",             (string, optional) An output descriptor\n"
            "        {                         (object, optional) An object with output descriptor and metadata\n"
            "          \"desc\": \"descriptor\",   (string, required) An output descriptor\n"
            "          \"range\": n,             (numeric, optional) Up to what child index HD chains should be explored (default: 1000)\n"
            "        },\n"
            "        ...\n"
            "    ]\n"
            "\nResult:\n"
            "{\n"
            "  \"unspents\": [\n"
            "    {\n"
            "    \"txid\" : \"transactionid\",     (string) The transaction id\n"
            "    \"vout\": n,                    (numeric) the vout value\n"
            "    \"scriptPubKey\" : \"script\",    (string) the script key\n"
            "    \"amount\" : x.xxx,             (numeric) The total amount in " + CURRENCY_UNIT + " of the unspent output\n"
            "    \"height\" : n,                 (numeric) Height of the unspent transaction output\n"
            "   }\n"
            "   ,...], \n"
            " \"total_amount\" : x.xxx,          (numeric) The total amount of all found unspent outputs in " + CURRENCY_UNIT + "\n"
            "]\n"
        );

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VARR});

    UniValue result(UniValue::VOBJ);
    if (request.params[0].get_str() == "status") {
        CoinsViewScanReserver reserver;
        if (reserver.reserve()) {
            // no scan in progress
            return NullUniValue;
        }
        result.pushKV("progress", g_scan_progress);
        return result;
    } else if (request.params[0].get_str() == "abort") {
        CoinsViewScanReserver reserver;
        if (reserver.reserve()) {
            // reserve was possible which means no scan was running
            return false;
        }
        // set the abort flag
        g_should_abort_scan = true;
        return true;
    } else if (request.params[0].get_str() == "start") {
        CoinsViewScanReserver reserver;
        if (!reserver.reserve()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Scan already in progress, use action \"abort\" or \"status\"");
        }
        std::set<CScript> needles;
        TxColoredCoinBalancesMap total_in;

        // loop through the scan objects
        for (const UniValue& scanobject : request.params[1].get_array().getValues()) {
            std::string desc_str;
            int range = 1000;
            if (scanobject.isStr()) {
                desc_str = scanobject.get_str();
            } else if (scanobject.isObject()) {
                UniValue desc_uni = scanobject.find_value("desc");
                if (desc_uni.isNull()) throw JSONRPCError(RPC_INVALID_PARAMETER, "Descriptor needs to be provided in scan object");
                desc_str = desc_uni.get_str();
                UniValue range_uni = scanobject.find_value("range");
                if (!range_uni.isNull()) {
                    range = range_uni.get_int();
                    if (range < 0 || range > 1000000) throw JSONRPCError(RPC_INVALID_PARAMETER, "range out of range");
                }
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Scan object needs to be either a string or an object");
            }

            FlatSigningProvider provider;
            auto desc = Parse(desc_str, provider);
            if (!desc) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid descriptor '%s'", desc_str));
            }
            if (!desc->IsRange()) range = 0;
            for (int i = 0; i <= range; ++i) {
                std::vector<CScript> scripts;
                if (!desc->Expand(i, provider, scripts, provider)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Cannot derive script without private keys: '%s'", desc_str));
                }
                needles.insert(scripts.begin(), scripts.end());
            }
        }

        // Scan the unspent transaction output set for inputs
        UniValue unspents(UniValue::VARR);
        std::vector<CTxOut> input_txos;
        std::map<COutPoint, Coin> coins;
        g_should_abort_scan = false;
        g_scan_progress = 0;
        int64_t count = 0;
        std::unique_ptr<CCoinsViewCursor> pcursor;
        {
            LOCK(cs_main);
            FlushStateToDisk();
            pcursor = std::unique_ptr<CCoinsViewCursor>(pcoinsdbview->Cursor());
            assert(pcursor);
        }
        bool res = FindScriptPubKey(g_scan_progress, g_should_abort_scan, count, pcursor.get(), needles, coins);
        result.pushKV("success", res);
        result.pushKV("searched_items", count);

        for (const auto& it : coins) {
            const COutPoint& outpoint = it.first;
            const Coin& coin = it.second;
            const CTxOut& txo = coin.out;
            input_txos.push_back(txo);
            ColorIdentifier colorId(GetColorIdFromScript(txo.scriptPubKey));
            total_in[colorId] += txo.nValue;

            UniValue unspent(UniValue::VOBJ);
            unspent.pushKV("txid", outpoint.hashMalFix.GetHex());
            unspent.pushKV("vout", (int32_t)outpoint.n);
            unspent.pushKV("scriptPubKey", HexStr(txo.scriptPubKey.begin(), txo.scriptPubKey.end()));
            unspent.pushKV("token", colorId.toHexString());
            unspent.pushKV("amount", (colorId.type == TokenTypes::NONE ? ValueFromAmount(txo.nValue) : txo.nValue ));
            unspent.pushKV("height", (int32_t)coin.nHeight);

            unspents.push_back(unspent);
        }
        result.pushKV("unspents", unspents);
        UniValue total(UniValue::VOBJ);
        for(auto single_in : total_in)
            total.pushKV(single_in.first.toHexString(), (single_in.first.type == TokenTypes::NONE ? ValueFromAmount(single_in.second) : single_in.second ));
        result.pushKV("total_amount", total);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid command");
    }
    return result;
}

static void CreateUTXOSnapshot(
    CAutoFile& afile,
    const fs::path& path,
    const fs::path& temppath,
    UniValue& result)
{
    std::unique_ptr<CCoinsViewCursor> pcursor;
    const CBlockIndex* tip;
    CCoinsStats maybe_stats;

    {
        // We need to lock cs_main to ensure that the coinsdb isn't written to
        // between (i) flushing coins cache to disk (coinsdb), (ii) getting stats
        // based upon the coinsdb, and (iii) constructing a cursor to the
        // coinsdb for use below this block.
        //
        // Cursors returned by leveldb iterate over snapshots, so the contents
        // of the pcursor will not be affected by simultaneous writes during
        // use below this block.
        //
        // See discussion here:
        //   https://github.com/bitcoin/bitcoin/pull/15606#discussion_r274479369
        //
        LOCK(::cs_main);

        FlushStateToDisk();

        auto success = GetUTXOStats(pcoinsdbview.get(), maybe_stats);
        if (!success) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
        }

        pcursor = std::unique_ptr<CCoinsViewCursor>(pcoinsdbview.get()->Cursor());
        tip = LookupBlockIndex(maybe_stats.hashBlock);
    }

    LogPrint(BCLog::RPC, "writing UTXO snapshot at height %d (%s) to file %s (via %s)\n",
        tip->nHeight, tip->GetBlockHash().ToString().c_str(),
        path, temppath);

    SnapshotMetadata metadata{tip->GetBlockHash(), maybe_stats.nTransactionOutputs};

    afile << metadata;

    COutPoint key;
    Coin coin;
    unsigned int iter{0};
    uint256 last_hash;
    size_t written_coins_count{0};
    std::vector<std::pair<uint32_t, Coin>> coins;

    // To reduce space the serialization format of the snapshot avoids
    // duplication of tx hashes. The code takes advantage of the guarantee by
    // leveldb that keys are lexicographically sorted.
    // In the coins vector we collect all coins that belong to a certain tx hash
    // (key.hash) and when we have them all (key.hash != last_hash) we write
    // them to file using the below lambda function.
    // See also https://github.com/bitcoin/bitcoin/issues/25675
    auto write_coins_to_file = [&](CAutoFile& afile, const uint256& last_hash, const std::vector<std::pair<uint32_t, Coin>>& coins, size_t& written_coins_count) {
        afile << last_hash;
        WriteCompactSize(afile, coins.size());
        for (const auto& [n, coin] : coins) {
            WriteCompactSize(afile, n);
            afile << coin;
            ++written_coins_count;
        }
    };
    while (pcursor->Valid()) {
        if (iter % 5000 == 0) ShutdownRequested();
        ++iter;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            if (key.hashMalFix != last_hash) {
                write_coins_to_file(afile, last_hash, coins, written_coins_count);
                last_hash = key.hashMalFix;
                coins.clear();
            }
            coins.emplace_back(key.n, coin);
        }
        pcursor->Next();
    }

    if (!coins.empty()) {
        write_coins_to_file(afile, last_hash, coins, written_coins_count);
    }

    result.pushKV("base_hash", tip->GetBlockHash().ToString());
    result.pushKV("base_height", tip->nHeight);
    result.pushKV("nchaintx", tip->nChainTx);
    result.pushKV("coins_written", maybe_stats.nTransactionOutputs);
    result.pushKV("txoutset_hash", maybe_stats.hashSerialized.ToString());
    return;
}

/**
 * Serialize the UTXO set to a file for loading elsewhere.
 *
 * @see SnapshotMetadata
 */
static UniValue dumptxoutset(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
        "dumptxoutset \"path\""
        "Write the serialized UTXO set to disk."
        "\nArguments:\n"
            "1. path     -   Path to the output file. If relative, will be prefixed by datadir.\n"
         "\nResult:\n"
                "coins_written  - the number of coins written in the snapshot\n"
                "base_hash  -  the hash of the base of the snapshot\n"
                "base_height  - the height of the base of the snapshot\n"
                "path  - the absolute path that the snapshot was written to\n"
                "txoutset_hash  - the hash of the UTXO set contents\n"
                "nchaintx  - the number of transactions in the chain up to and including the base block\n");

    const std::string path_name = request.params[0].get_str();
    // Reject absolute paths and any component that would traverse above datadir.
    {
        const fs::path rel(path_name);
        if (rel.is_absolute()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Path must be relative");
        }
        for (const auto& component : rel) {
            if (component == "..") {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Path must not contain '..'");
            }
        }
    }
    const fs::path path = GetDataDir() / path_name;
    // Resolve symlinks in the parent directory and verify the result stays
    // inside datadir — prevents traversal via a symlink that points elsewhere.
    {
        const fs::path parent = path.parent_path();
        if (fs::exists(parent)) {
            const fs::path canonical_parent = fs::canonical(parent);
            const fs::path canonical_datadir = fs::canonical(GetDataDir());
            // fs::relative returns ".." components when canonical_parent is outside
            // canonical_datadir, or an empty path when the two roots differ (e.g.
            // different drive letters on Windows).  Either case means the path
            // escapes the data directory.
            const fs::path rel = fs::relative(canonical_parent, canonical_datadir);
            if (rel.empty() || *rel.begin() == "..") {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Path resolves outside the data directory");
            }
        }
    }
    // Write to a temporary path and then move into `path` on completion
    // to avoid confusion due to an interruption.
    const fs::path temppath = fs::path(path.string() + ".incomplete");

    if (fs::exists(path)) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "path already exists. If you are sure this is what you want, move it out of the way first");
    }

    FILE* file{fsbridge::fopen(temppath, "wb")};
    CAutoFile afile{file, SER_DISK, CLIENT_VERSION};
    if (afile.IsNull()) {
        throw JSONRPCError(
            RPC_INVALID_PARAMETER,
            "Couldn't open file temp file for writing.");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("path", path.c_str());

    //snapshot in temp file
    CreateUTXOSnapshot(afile, path, temppath, result);

    //move snapshot to actual path
    fs::rename(temppath, path);

    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "blockchain",         "gettxoutsetinfo",        &gettxoutsetinfo,        {} },
    { "blockchain",         "scantxoutset",           &scantxoutset,           {"action", "scanobjects"} },
    { "blockchain",         "dumptxoutset",           &dumptxoutset,               {"path"} },
};

void RegisterUTXOSetRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
