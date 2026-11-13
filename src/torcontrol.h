// Copyright (c) 2015-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Functionality for communicating with Tor.
 */
#ifndef BITCOIN_TORCONTROL_H
#define BITCOIN_TORCONTROL_H

#include <netaddress.h>
#include <scheduler.h>

#include <cstdint>
#include <string>

extern const std::string DEFAULT_TOR_CONTROL;
static const bool DEFAULT_LISTEN_ONION = true;

/** The ADD_ONION request for the onion service this node creates. */
class TorAddOnionCommand
{
public:
    //! Asks Tor for a new v3 onion service key; Tor 0.4.6 and later reject the old RSA1024 (v2) type.
    static constexpr const char* NEW_KEY = "NEW:ED25519-V3";

    //! Tor forwards connections on virtual_port of the onion service to target.
    //! An empty private_key requests a new key.
    static std::string Build(const std::string& private_key, uint16_t virtual_port, const CService& target);
};

void StartTorControl(const CService& onion_service_target);
void InterruptTorControl();
void StopTorControl();

#endif /* BITCOIN_TORCONTROL_H */
