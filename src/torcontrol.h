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
    //! Tor forwards connections on virtual_port of the onion service to target.
    static std::string Build(const std::string& private_key, uint16_t virtual_port, const CService& target);
};

void StartTorControl(const CService& onion_service_target);
void InterruptTorControl();
void StopTorControl();

#endif /* BITCOIN_TORCONTROL_H */
