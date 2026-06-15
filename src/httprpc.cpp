// Copyright (c) 2015-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <httprpc.h>

#include <chainparams.h>
#include <httpserver.h>
#include <key_io.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <random.h>
#include <sync.h>
#include <util.h>
#include <utilstrencodings.h>
#include <ui_interface.h>
#include <crypto/hmac_sha256.h>
#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <boost/algorithm/string.hpp> // boost::trim

/** WWW-Authenticate to present with 401 Unauthorized response */
static const char* WWW_AUTH_HEADER_DATA = "Basic realm=\"jsonrpc\"";

/** Simple one-shot callback timer to be used by the RPC mechanism to e.g.
 * re-lock the wallet.
 */
class HTTPRPCTimer : public RPCTimerBase
{
public:
    HTTPRPCTimer(struct event_base* eventBase, std::function<void(void)>& func, int64_t millis) :
        ev(eventBase, false, func)
    {
        struct timeval tv;
        tv.tv_sec = millis/1000;
        tv.tv_usec = (millis%1000)*1000;
        ev.trigger(&tv);
    }
private:
    HTTPEvent ev;
};

class HTTPRPCTimerInterface : public RPCTimerInterface
{
public:
    explicit HTTPRPCTimerInterface(struct event_base* _base) : base(_base)
    {
    }
    const char* Name() override
    {
        return "HTTP";
    }
    RPCTimerBase* NewTimer(std::function<void(void)>& func, int64_t millis) override
    {
        return new HTTPRPCTimer(base, func, millis);
    }
private:
    struct event_base* base;
};


/* Pre-base64-encoded authentication token */
static std::string strRPCUserColonPass;
/* Stored RPC timer interface (for unregistration) */
static std::unique_ptr<HTTPRPCTimerInterface> httpRPCTimerInterface;
/* RPC Auth Whitelist */
static std::map<std::string, std::set<std::string>> g_rpc_whitelist;
static bool g_rpc_whitelist_default = false;

/* Per-IP failed-auth backoff. State for one peer address. */
struct AuthFailState {
    int64_t next_allowed_ms{0}; // earliest time (GetTimeMillis) a new attempt is accepted
    int consecutive{0};         // consecutive failure count; reset on success
};

// Max number of IP entries kept in the backoff table.
// When exceeded, expired entries are pruned; if still over, the worst offender is evicted.
static constexpr size_t AUTH_FAIL_CACHE_MAX = 1024;

static Mutex g_auth_fail_mutex;
static std::map<std::string, AuthFailState> g_failed_auths GUARDED_BY(g_auth_fail_mutex);
// Mirror of g_failed_auths.size(), readable without the mutex for the success-path fast path.
// All writes are performed under g_auth_fail_mutex; only the lock-free read on the success
// path races — a stale zero causes a missed erase (entry expires naturally) and a stale
// nonzero causes a no-op erase under the lock.  Both are harmless.
static std::atomic<size_t> g_failed_auths_size{0};

// Backoff for the n-th consecutive failure: 250ms, 500ms, 1s, … capped at 32s.
static int64_t AuthBackoffMs(int n)
{
    return 250LL << std::min(n - 1, 7);
}


static void JSONErrorReply(HTTPRequest* req, const UniValue& objError, const UniValue& id)
{
    // Send error reply from json-rpc error object
    int nStatus = HTTP_INTERNAL_SERVER_ERROR;
    int code = objError.find_value("code").get_int();

    if (code == RPC_INVALID_REQUEST)
        nStatus = HTTP_BAD_REQUEST;
    else if (code == RPC_METHOD_NOT_FOUND)
        nStatus = HTTP_NOT_FOUND;

    std::string strReply = JSONRPCReply(NullUniValue, objError, id);

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(nStatus, strReply);
}

//This function checks username and password against -rpcauth
//entries from config file.
static bool multiUserAuthorized(std::string strUserPass)
{
    if (strUserPass.find(':') == std::string::npos) {
        return false;
    }
    std::string strUser = strUserPass.substr(0, strUserPass.find(':'));
    std::string strPass = strUserPass.substr(strUserPass.find(':') + 1);

    for (const std::string& strRPCAuth : gArgs.GetArgs("-rpcauth")) {
        //Search for multi-user login/pass "rpcauth" from config
        std::vector<std::string> vFields;
        boost::split(vFields, strRPCAuth, boost::is_any_of(":$"));
        if (vFields.size() != 3) {
            //Incorrect formatting in config file
            continue;
        }

        std::string strName = vFields[0];
        if (!TimingResistantEqual(strName, strUser)) {
            continue;
        }

        std::string strSalt = vFields[1];
        std::string strHash = vFields[2];

        static const unsigned int KEY_SIZE = 32;
        unsigned char out[KEY_SIZE];

        CHMAC_SHA256(reinterpret_cast<const unsigned char*>(strSalt.c_str()), strSalt.size()).Write(reinterpret_cast<const unsigned char*>(strPass.c_str()), strPass.size()).Finalize(out);
        std::vector<unsigned char> hexvec(out, out+KEY_SIZE);
        std::string strHashFromPass = HexStr(hexvec);

        if (TimingResistantEqual(strHashFromPass, strHash)) {
            return true;
        }
    }
    return false;
}

static bool RPCAuthorized(const std::string& strAuth, std::string& strAuthUsernameOut)
{
    if (strRPCUserColonPass.empty()) // Belt-and-suspenders measure if InitRPCAuthentication was not called
        return false;
    if (strAuth.substr(0, 6) != "Basic ")
        return false;
    std::string strUserPass64 = strAuth.substr(6);
    // Trim left
    strUserPass64.erase(strUserPass64.begin(),
                        std::find_if(strUserPass64.begin(), strUserPass64.end(),
                                    [](char c) { return !IsSpace(c); }));
    // Trim right
    strUserPass64.erase(std::find_if(strUserPass64.rbegin(), strUserPass64.rend(),
                                     [](char c) { return !IsSpace(c); }).base(),
                        strUserPass64.end());
    std::string strUserPass = DecodeBase64(strUserPass64);

    if (strUserPass.find(':') != std::string::npos)
        strAuthUsernameOut = strUserPass.substr(0, strUserPass.find(':'));

    //Check if authorized under single-user field
    if (TimingResistantEqual(strUserPass, strRPCUserColonPass)) {
        return true;
    }
    return multiUserAuthorized(strUserPass);
}

static bool HTTPReq_JSONRPC(HTTPRequest* req, const std::string &)
{
    // JSONRPC handles only POST
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "JSONRPC server handles only POST requests");
        return false;
    }
    // Check authorization
    std::pair<bool, std::string> authHeader = req->GetHeader("authorization");
    if (!authHeader.first) {
        req->WriteHeader("WWW-Authenticate", WWW_AUTH_HEADER_DATA);
        req->WriteReply(HTTP_UNAUTHORIZED);
        return false;
    }

    JSONRPCRequest jreq;
    CService peer = req->GetPeer();
    jreq.peerAddr = peer.ToString();
    if (!RPCAuthorized(authHeader.second, jreq.authUser)) {
        const std::string peerIP = peer.ToStringIP();
        const int64_t now = GetTimeMillis();
        bool in_backoff = false;
        {
            LOCK(g_auth_fail_mutex);
            // Keep the table bounded: first remove expired entries, then if still full
            // evict the worst offender (largest next_allowed_ms = deepest backoff).
            // Evicting the highest-backoff entry removes an attacker, preserving
            // legitimate users whose backoff is smallest.  Never clear() the entire
            // table — that would let an attacker with 1025 IPs reset all throttled
            // peers simultaneously.
            if (g_failed_auths.size() >= AUTH_FAIL_CACHE_MAX) {
                for (auto it = g_failed_auths.begin(); it != g_failed_auths.end(); ) {
                    if (it->second.next_allowed_ms <= now) {
                        it = g_failed_auths.erase(it);
                        --g_failed_auths_size;
                    } else {
                        ++it;
                    }
                }
                if (g_failed_auths.size() >= AUTH_FAIL_CACHE_MAX) {
                    auto victim = std::max_element(g_failed_auths.begin(), g_failed_auths.end(),
                        [](const auto& a, const auto& b) {
                            return a.second.next_allowed_ms < b.second.next_allowed_ms;
                        });
                    g_failed_auths.erase(victim);
                    --g_failed_auths_size;
                }
            }
            auto [it, inserted] = g_failed_auths.emplace(peerIP, AuthFailState{});
            if (inserted) ++g_failed_auths_size;
            AuthFailState& state = it->second;
            if (now < state.next_allowed_ms) {
                in_backoff = true;
            } else {
                state.consecutive++;
                state.next_allowed_ms = now + AuthBackoffMs(state.consecutive);
            }
        } // g_auth_fail_mutex released; I/O runs unlocked
        if (!in_backoff) {
            LogPrintf("ThreadRPCServer incorrect password attempt from %s\n", jreq.peerAddr);
        }
        req->WriteHeader("WWW-Authenticate", WWW_AUTH_HEADER_DATA);
        req->WriteReply(HTTP_UNAUTHORIZED);
        return false;
    }
    // Successful auth: clear any backoff for this IP.
    // Fast path: skip the lock entirely when the table is empty (common case for
    // legitimate users who have never triggered a failure entry).
    if (g_failed_auths_size.load(std::memory_order_relaxed) > 0) {
        const std::string peerIP = peer.ToStringIP();
        LOCK(g_auth_fail_mutex);
        if (g_failed_auths.erase(peerIP))
            --g_failed_auths_size;
    }

    try {
        // Parse request
        UniValue valRequest;
        if (!valRequest.read(req->ReadBody()))
            throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");

        // Set the URI
        jreq.URI = req->GetURI();

        std::string strReply;
        bool user_has_whitelist = g_rpc_whitelist.count(jreq.authUser);
        if (!user_has_whitelist && g_rpc_whitelist_default) {
            LogPrintf("RPC User %s not allowed to call any methods\n", jreq.authUser);
            req->WriteReply(HTTP_FORBIDDEN);
            return false;

        // singleton request
        } else if (valRequest.isObject()) {
            jreq.parse(valRequest);
            if (user_has_whitelist && !g_rpc_whitelist[jreq.authUser].count(jreq.strMethod)) {
                LogPrintf("RPC User %s not allowed to call method %s\n", jreq.authUser, jreq.strMethod);
                req->WriteReply(HTTP_FORBIDDEN);
                return false;
            }
            UniValue result = tableRPC.execute(jreq);

            // Send reply
            strReply = JSONRPCReply(result, NullUniValue, jreq.id);

        // array of requests
        } else if (valRequest.isArray()) {
            if (user_has_whitelist) {
                for (unsigned int reqIdx = 0; reqIdx < valRequest.size(); reqIdx++) {
                    if (!valRequest[reqIdx].isObject()) {
                        throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
                    } else {
                        const UniValue& request = valRequest[reqIdx].get_obj();
                        // Parse method
                        std::string strMethod = request.find_value("method").get_str();
                        if (!g_rpc_whitelist[jreq.authUser].count(strMethod)) {
                            LogPrintf("RPC User %s not allowed to call method %s\n", jreq.authUser, strMethod);
                            req->WriteReply(HTTP_FORBIDDEN);
                            return false;
                        }
                    }
                }
            }
            strReply = JSONRPCExecBatch(jreq, valRequest.get_array());
        }
        else
            throw JSONRPCError(RPC_PARSE_ERROR, "Top-level object parse error");

        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strReply);
    } catch (const UniValue& objError) {
        JSONErrorReply(req, objError, jreq.id);
        return false;
    } catch (const std::exception& e) {
        JSONErrorReply(req, JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
        return false;
    }
    return true;
}

static bool InitRPCAuthentication()
{
    if (gArgs.GetArg("-rpcpassword", "") == "")
    {
        LogPrintf("No rpcpassword set - using random cookie authentication.\n");
        if (!GenerateAuthCookie(&strRPCUserColonPass)) {
            uiInterface.ThreadSafeMessageBox(
                _("Error: A fatal internal error occurred, see debug.log for details"), // Same message as AbortNode
                "", CClientUIInterface::MSG_ERROR);
            return false;
        }
    } else {
        LogPrintf("Config options rpcuser and rpcpassword will soon be deprecated. Locally-run instances may remove rpcuser to use cookie-based auth, or may be replaced with rpcauth. Please see share/rpcauth for rpcauth auth generation.\n");
        strRPCUserColonPass = gArgs.GetArg("-rpcuser", "") + ":" + gArgs.GetArg("-rpcpassword", "");
    }
    if (gArgs.GetArg("-rpcauth","") != "")
    {
        LogPrintf("Using rpcauth authentication.\n");
    }

    g_rpc_whitelist_default = gArgs.GetBoolArg("-rpcwhitelistdefault", gArgs.IsArgSet("-rpcwhitelist"));
    for (const std::string& strRPCWhitelist : gArgs.GetArgs("-rpcwhitelist")) {
        auto pos = strRPCWhitelist.find(':');
        std::string strUser = strRPCWhitelist.substr(0, pos);
        bool intersect = g_rpc_whitelist.count(strUser);
        std::set<std::string>& whitelist = g_rpc_whitelist[strUser];
        if (pos != std::string::npos) {
            std::string strWhitelist = strRPCWhitelist.substr(pos + 1);
            std::set<std::string> new_whitelist;
            boost::split(new_whitelist, strWhitelist, boost::is_any_of(", "));
            if (intersect) {
                std::set<std::string> tmp_whitelist;
                std::set_intersection(new_whitelist.begin(), new_whitelist.end(),
                       whitelist.begin(), whitelist.end(), std::inserter(tmp_whitelist, tmp_whitelist.end()));
                new_whitelist = std::move(tmp_whitelist);
            }
            whitelist = std::move(new_whitelist);
        }
    }

    return true;
}

bool StartHTTPRPC()
{
    LogPrint(BCLog::RPC, "Starting HTTP RPC server\n");
    if (!InitRPCAuthentication())
        return false;

    RegisterHTTPHandler("/", true, HTTPReq_JSONRPC);
#if ENABLE_WALLET
    // ifdef can be removed once we switch to better endpoint support and API versioning
    RegisterHTTPHandler("/wallet/", false, HTTPReq_JSONRPC);
#endif
    assert(EventBase());
    httpRPCTimerInterface = MakeUnique<HTTPRPCTimerInterface>(EventBase());
    RPCSetTimerInterface(httpRPCTimerInterface.get());
    return true;
}

void InterruptHTTPRPC()
{
    LogPrint(BCLog::RPC, "Interrupting HTTP RPC server\n");
}

void StopHTTPRPC()
{
    LogPrint(BCLog::RPC, "Stopping HTTP RPC server\n");
    UnregisterHTTPHandler("/", true);
#if ENABLE_WALLET
    UnregisterHTTPHandler("/wallet/", false);
#endif
    if (httpRPCTimerInterface) {
        RPCUnsetTimerInterface(httpRPCTimerInterface.get());
        httpRPCTimerInterface.reset();
    }
}
