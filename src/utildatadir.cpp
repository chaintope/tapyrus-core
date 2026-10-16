// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util.h>

#ifdef WIN32
#ifdef _MSC_VER
#pragma warning(disable:4786)
#pragma warning(disable:4804)
#pragma warning(disable:4805)
#pragma warning(disable:4717)
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501

#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501

#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <shlobj.h>
#endif

fs::path GetDefaultDataDir()
{
    // Windows < Vista: C:\Documents and Settings\Username\Application Data\Tapyrus
    // Windows >= Vista: C:\Users\Username\AppData\Roaming\Tapyrus
    // Mac: ~/Library/Application Support/Tapyrus
    // Unix: ~/.tapyrus
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath(CSIDL_APPDATA) / "Tapyrus";
#else
    fs::path pathRet;
    char* pszHome = getenv("HOME");
    if (pszHome == nullptr || strlen(pszHome) == 0)
        pathRet = fs::path("/");
    else
        pathRet = fs::path(pszHome);
#ifdef __APPLE__
    // Mac
    return pathRet / "Library/Application Support/Tapyrus";
#else
    // Unix
    return pathRet / ".tapyrus";
#endif
#endif
}

static fs::path g_blocks_path_cache_net_specific;
static fs::path pathCached;
static fs::path pathCachedNetSpecific;
static RecursiveMutex csPathCached;

std::string GetDataDirNameFromNetworkId(const uint32_t networkId)
{
    const std::string strNetworkId(std::to_string(networkId));
    return TAPYRUS_MODES::GetChainName(gArgs.GetChainMode()) + (strNetworkId.size() ? "-" + strNetworkId : "");
}

const fs::path &GetBlocksDir()
{

    LOCK(csPathCached);

    fs::path &path = g_blocks_path_cache_net_specific;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (gArgs.IsArgSet("-blocksdir")) {
        path = fs::absolute(gArgs.GetArg("-blocksdir", ""));
        if (!fs::is_directory(path)) {
            path = "";
            return path;
        }
    } else {
        path = GetDataDir(false);
    }

    path /= GetDataDirNameFromNetworkId(gArgs.GetArg("-networkid", TAPYRUS_MODES::GetDefaultNetworkId(gArgs.GetChainMode())));

    path /= "blocks";
    fs::create_directories(path);
    return path;
}

const fs::path &GetDataDir(bool fNetSpecific)
{

    LOCK(csPathCached);

    fs::path &path = fNetSpecific ? pathCachedNetSpecific : pathCached;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (gArgs.IsArgSet("-datadir")) {
        path = fs::absolute(gArgs.GetArg("-datadir", ""));
        if (!fs::is_directory(path)) {
            path = "";
            return path;
        }
    } else {
        path = GetDefaultDataDir();
    }
    if (fNetSpecific)
        path /= GetDataDirNameFromNetworkId(gArgs.GetArg("-networkid", TAPYRUS_MODES::GetDefaultNetworkId(gArgs.GetChainMode())));

    if (fs::create_directories(path)) {
        // This is the first run, create wallets subdirectory too
        fs::create_directories(path / "wallets");
    }

    return path;
}

void ClearDatadirCache()
{
    LOCK(csPathCached);

    pathCached = fs::path();
    pathCachedNetSpecific = fs::path();
    g_blocks_path_cache_net_specific = fs::path();
}

fs::path GetConfigFile(const std::string& confPath)
{
    return AbsPathForConfigVal(fs::path(confPath), false);
}

#ifndef WIN32
fs::path GetPidFile()
{
    return AbsPathForConfigVal(fs::path(gArgs.GetArg("-pid", BITCOIN_PID_FILENAME)));
}
#endif

#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate)
{
    char pszPath[MAX_PATH] = "";

    if(SHGetSpecialFolderPathA(nullptr, pszPath, nFolder, fCreate))
    {
        return fs::path(pszPath);
    }

    LogPrintf("SHGetSpecialFolderPathA() failed, could not obtain requested path.\n");
    return fs::path("");
}
#endif

fs::path AbsPathForConfigVal(const fs::path& in_path, bool net_specific)
{
    if (in_path.is_absolute()) {
        return in_path;
    }
    return fs::absolute(( GetDataDir(net_specific) / in_path));
}
