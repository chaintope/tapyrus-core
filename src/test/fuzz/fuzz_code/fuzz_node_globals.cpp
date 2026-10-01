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

// AbortNode() (validation.cpp) calls this after a fatal internal error.
// Aborting rather than exiting cleanly makes libFuzzer report it as a
// crash and write an artifact: an exit(0) here would look like a normal
// run to both libFuzzer and the CI loop, hiding exactly the failures a
// node-context harness exists to find.
[[noreturn]] void StartShutdown()
{
    std::abort();
}

bool ShutdownRequested()
{
    return false;
}
