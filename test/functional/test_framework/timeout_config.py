#!/usr/bin/env python3
# Copyright (c) 2019 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Timeout configuration constants for functional tests.

This module defines all timeout constants used across the test framework.
All timeouts are calculated as multiples of the immediate timeout (time_to_connect * 2).
It can be imported by any module without creating circular dependencies.
"""

# Default time_to_connect value (can be overridden by CI)
TIME_TO_CONNECT = 2  # Default: 2 seconds

# Timeout multipliers relative to immediate timeout
# immediate_timeout = time_to_connect * 2
# All other timeouts = immediate_timeout * multiplier

# Maximum duration for any single test
TOTAL_TEST_DURATION_MULTIPLIER = 900  # 900x immediate (3600s = 60min with immediate=4s)

# Node synchronization timeout - time for multiple nodes to sync  
TAPYRUSD_SYNC_TIMEOUT_MULTIPLIER = 225  # 225x immediate (900s = 15min with immediate=4s)

# Complex operations timeout - time to wait for tapyrusd to start with reindex/reorg
TAPYRUSD_REORG_TIMEOUT_MULTIPLIER = 150  # 150x immediate (600s = 10min with immediate=4s)

# Process startup timeout - time to wait for tapyrusd to start normally
TAPYRUSD_PROC_TIMEOUT_MULTIPLIER = 75  # 75x immediate (300s = 5min with immediate=4s)

# Message response timeout - time to wait for a response from tapyrusd
TAPYRUSD_MESSAGE_TIMEOUT_MULTIPLIER = 30  # 30x immediate (120s = 2min with immediate=4s)

# P2P message operations timeout
TAPYRUSD_P2P_TIMEOUT_MULTIPLIER = 7.5  # 7.5x immediate (30s with immediate=4s)

# Minimum timeout - shortest timeout for quick operations
TAPYRUSD_MIN_TIMEOUT_MULTIPLIER = 1.25  # 1.25x immediate (5s with immediate=4s)


def set_time_to_connect(time_to_connect):
    """Set the time_to_connect value that affects all timeouts.
    
    Args:
        time_to_connect (int): Base connection timeout in seconds
    """
    global TIME_TO_CONNECT
    TIME_TO_CONNECT = max(1, int(time_to_connect))  # Never less than 1 second


def get_immediate_timeout():
    """Get immediate timeout calculated as 2 * time_to_connect.
    
    Returns:
        int: Immediate timeout in seconds (2 * TIME_TO_CONNECT)
    """
    return max(2, int(TIME_TO_CONNECT * 2))


def get_timeout(multiplier):
    """Calculate a timeout based on immediate timeout and multiplier.
    
    Args:
        multiplier (float): The multiplier to apply to immediate timeout
        
    Returns:
        int: Calculated timeout in seconds
    """
    immediate = get_immediate_timeout()
    return max(1, int(immediate * multiplier))


# Convenient functions to get specific timeouts
def get_total_test_duration():
    return get_timeout(TOTAL_TEST_DURATION_MULTIPLIER)

def get_sync_timeout():
    return get_timeout(TAPYRUSD_SYNC_TIMEOUT_MULTIPLIER)

def get_reorg_timeout():
    return get_timeout(TAPYRUSD_REORG_TIMEOUT_MULTIPLIER)

def get_proc_timeout():
    return get_timeout(TAPYRUSD_PROC_TIMEOUT_MULTIPLIER)

def get_message_timeout():
    return get_timeout(TAPYRUSD_MESSAGE_TIMEOUT_MULTIPLIER)

def get_p2p_timeout():
    return get_timeout(TAPYRUSD_P2P_TIMEOUT_MULTIPLIER)

def get_min_timeout():
    return get_timeout(TAPYRUSD_MIN_TIMEOUT_MULTIPLIER)


# Timeout constants - these will be dynamically calculated
TOTAL_TEST_DURATION = get_total_test_duration()
TAPYRUSD_SYNC_TIMEOUT = get_sync_timeout()
TAPYRUSD_REORG_TIMEOUT = get_reorg_timeout()
TAPYRUSD_PROC_TIMEOUT = get_proc_timeout()
TAPYRUSD_MESSAGE_TIMEOUT = get_message_timeout()
TAPYRUSD_P2P_TIMEOUT = get_p2p_timeout()
TAPYRUSD_MIN_TIMEOUT = get_min_timeout()
TAPYRUSD_IMMEDIATE_TIMEOUT = get_immediate_timeout()


def update_all_timeouts():
    """Update all timeout global variables after changing TIME_TO_CONNECT.
    
    This should be called after set_time_to_connect() to update all globals.
    """
    global TOTAL_TEST_DURATION, TAPYRUSD_SYNC_TIMEOUT, TAPYRUSD_REORG_TIMEOUT
    global TAPYRUSD_PROC_TIMEOUT, TAPYRUSD_MESSAGE_TIMEOUT, TAPYRUSD_P2P_TIMEOUT
    global TAPYRUSD_MIN_TIMEOUT, TAPYRUSD_IMMEDIATE_TIMEOUT
    
    TOTAL_TEST_DURATION = get_total_test_duration()
    TAPYRUSD_SYNC_TIMEOUT = get_sync_timeout()
    TAPYRUSD_REORG_TIMEOUT = get_reorg_timeout()
    TAPYRUSD_PROC_TIMEOUT = get_proc_timeout()
    TAPYRUSD_MESSAGE_TIMEOUT = get_message_timeout()
    TAPYRUSD_P2P_TIMEOUT = get_p2p_timeout()
    TAPYRUSD_MIN_TIMEOUT = get_min_timeout()
    TAPYRUSD_IMMEDIATE_TIMEOUT = get_immediate_timeout()


def get_all_timeouts():
    """Return a dictionary of all current timeout values."""
    return {
        'TIME_TO_CONNECT': TIME_TO_CONNECT,
        'TAPYRUSD_IMMEDIATE_TIMEOUT': get_immediate_timeout(),
        'TOTAL_TEST_DURATION': get_total_test_duration(),
        'TAPYRUSD_SYNC_TIMEOUT': get_sync_timeout(),
        'TAPYRUSD_REORG_TIMEOUT': get_reorg_timeout(),
        'TAPYRUSD_PROC_TIMEOUT': get_proc_timeout(),
        'TAPYRUSD_MESSAGE_TIMEOUT': get_message_timeout(),
        'TAPYRUSD_P2P_TIMEOUT': get_p2p_timeout(),
        'TAPYRUSD_MIN_TIMEOUT': get_min_timeout()
    }