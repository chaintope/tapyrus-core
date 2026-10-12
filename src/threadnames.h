// Copyright (c) 2018-2019 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_THREADNAMES_H
#define TAPYRUS_THREADNAMES_H

#include <string>

//! Set the calling thread's name as shown in log lines (-logthreadnames). Does not
//! rename the thread in the OS; RenameThread does both.
void ThreadSetInternalName(const std::string& name);

//! Get the calling thread's name as set by RenameThread or ThreadSetInternalName;
//! empty if neither was called on this thread.
std::string ThreadGetInternalName();

#endif // TAPYRUS_THREADNAMES_H
