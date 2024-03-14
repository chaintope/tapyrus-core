// Copyright (c) 2023 The Bitcoin Core developers
// Copyright (c) 2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cs_main.h>
#include <sync.h>

RecursiveMutex cs_main;
RecursiveMutex cs_LastBlockFile;
