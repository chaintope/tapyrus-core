// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Shared node context for fuzz targets that call RPC handlers or
// network-message-processing functions, which need a real chainstate,
// mempool, registered RPC table, and CConnman to run at all -- see
// test/test_tapyrus.h's own TestingSetup for exactly what gets
// initialized (5-block REGTEST-mode chain, LevelDB-backed coin/block
// databases, PeerLogicValidation, RegisterAllCoreRPCCommands). Reuses
// that class directly (via the tapyrus_test_util object library, see
// src/test/CMakeLists.txt) rather than reimplementing node setup for
// fuzzing -- the same fixture every *_tests.cpp Boost test already
// relies on.
//
// GetFuzzNodeSetup() constructs its FuzzNodeSetup exactly once per
// process and never destroys it: TestingSetup's constructor does real,
// expensive work (opens LevelDB databases, spawns a scheduler thread and
// script-check worker threads) that every fuzz input should reuse,
// matching libFuzzer's own persistent-process model. Deliberately
// heap-allocated and leaked rather than a function-local static: a
// function-local static IS still torn down by the C++ runtime's normal
// exit-time destructor unwinding, and TestingSetup's real teardown
// (closing LevelDB, joining threads) crashes (SIGBUS) when run that way
// from a plain main() return -- confirmed directly, the crash lands
// after test_one_input_<target> has already returned successfully.
// Call it as the first line of test_one_input_<target>, not from
// LLVMFuzzerInitialize -- pstt_fuzz_driver.h already defines that symbol
// for the ECCVerifyHandle every fuzz target needs, and only RPC/network
// targets pay TestingSetup's own construction cost.

#ifndef TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_NODE_SETUP_H
#define TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_NODE_SETUP_H

#include <rpc/server.h>
#include <test/test_tapyrus.h>

struct FuzzNodeSetup : public TestingSetup {
    // fRPCInWarmup (rpc/server.cpp) defaults to true and neither
    // BasicTestingSetup nor TestingSetup clears it -- every RPC call
    // through tableRPC.execute() would otherwise throw RPC_IN_WARMUP
    // unconditionally, regardless of fuzz input.
    FuzzNodeSetup() : TestingSetup(TAPYRUS_MODES::PROD) { SetRPCWarmupFinished(); }
};

inline FuzzNodeSetup& GetFuzzNodeSetup()
{
    static FuzzNodeSetup* const setup = new FuzzNodeSetup();
    return *setup;
}

#endif // TAPYRUS_TEST_FUZZ_FUZZ_CODE_FUZZ_NODE_SETUP_H
