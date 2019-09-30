// Copyright (c) 2012-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERSION_H
#define BITCOIN_VERSION_H

/**
 * network protocol versioning
 */

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
//! BIP 0031, pong message, is enabled for all versions AFTER this one
//! "filter*" commands are disabled without NODE_BLOOM after and including this version
//! "sendheaders" command and announcing blocks with headers starts with this version
//! "feefilter" tells peers to filter invs to you by fee starts with this version
//! short-id-based block download starts with this version
//! not banning for invalid compact blocks starts with this version
//! In this version, 'getheaders' was introduced.
static const int PROTOCOL_VERSION = 10000;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 10000;

//! disconnect from peers older than this proto version
static const int MIN_PEER_PROTO_VERSION = PROTOCOL_VERSION;

#endif // BITCOIN_VERSION_H
