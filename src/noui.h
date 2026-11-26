// Copyright (c) 2013-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NOUI_H
#define BITCOIN_NOUI_H

/** Connects the handlers that print messages to the console and the debug log; replaces earlier ones. */
extern void noui_connect();
/** Tests only: sends messages to the debug log, not the console, until noui_reconnect(). */
extern void noui_test_redirect();
/** Undoes noui_test_redirect(). */
extern void noui_reconnect();

#endif // BITCOIN_NOUI_H
