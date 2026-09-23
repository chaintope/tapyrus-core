// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// g_connman/StartShutdown()/ShutdownRequested() for fuzz targets that
// link fuzz_node_setup.h's FuzzNodeSetup (TestingSetup). Normally
// provided by test_tapyrus_main.cpp, which a fuzz binary can't also
// link: that file also defines Boost Test's own main(), which would
// collide with libFuzzer's/AFL's main() from pstt_fuzz_driver.h.
#include <net.h>

#include <cstdlib>
#include <memory>

std::unique_ptr<CConnman> g_connman;

[[noreturn]] void StartShutdown()
{
    std::exit(EXIT_SUCCESS);
}

bool ShutdownRequested()
{
    return false;
}
