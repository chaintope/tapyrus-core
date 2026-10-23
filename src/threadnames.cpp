// Copyright (c) 2018-2019 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <threadnames.h>

#include <algorithm>
#include <cstring>

/**
 * The name of the thread. A char array rather than std::string, so no
 * destructor runs when the thread exits.
 */
static thread_local char g_thread_name[128]{'\0'};

void ThreadSetInternalName(const std::string& name)
{
    const size_t copy_bytes{std::min(sizeof(g_thread_name) - 1, name.length())};
    std::memcpy(g_thread_name, name.data(), copy_bytes);
    g_thread_name[copy_bytes] = '\0';
}

std::string ThreadGetInternalName()
{
    return g_thread_name;
}
