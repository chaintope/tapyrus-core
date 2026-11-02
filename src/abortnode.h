// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2019-2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_ABORTNODE_H
#define TAPYRUS_ABORTNODE_H

#include <string>

class CValidationState;

/** Sets the warning, logs, shows the message and requests shutdown; returns false. */
bool AbortNode(const std::string& strMessage, const std::string& userMessage = "");
/** As above, and marks state as an error. */
bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage = "");

#endif // TAPYRUS_ABORTNODE_H
