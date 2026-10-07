// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for ComputeLocktime and
// PartiallySignedTapyrusTransaction::GetIdentifier (both src/pstt.cpp) --
// the two remaining functions in the module with real per-PSTT logic
// not already exercised by pstt_merge_fuzz.cpp/pstt_extract_fuzz.cpp/
// pstt_signer_fuzz.cpp/pstt_decodepstt_rpc_fuzz.cpp. Called directly and
// separately (not just via GetIdentifier, which calls ComputeLocktime
// internally) so a ComputeLocktime-only regression is directly
// attributable rather than only visible through GetIdentifier's own
// behavior.
//
// ComputeLocktime reconciles each input's required_height_locktime/
// required_time_locktime constraints (max-of-constraints per kind,
// height preferred when both are simultaneously satisfiable, refused
// outright when they conflict) into one locktime for the whole PSTT --
// pure arithmetic over already-decoded fields, no exceptions of its own.
// GetIdentifier builds on it to materialize an all-sequences-zeroed
// transaction and hash it, documented to throw std::runtime_error when
// ComputeLocktime finds no valid locktime.

#include <tapyrus-config.h>

#include <core_io.h>
#include <key.h>
#include <pstt.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

static int test_one_input_locktime(const uint8_t* data, size_t size)
{
    const std::string base64_pstt(reinterpret_cast<const char*>(data), size);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, base64_pstt, error)) return 0;

    uint32_t locktime_out = 0;
    const bool computed = ComputeLocktime(pstt, locktime_out);

    bool threw = false;
    try {
        pstt.GetIdentifier();
    } catch (const std::exception&) {
        // Documented to throw std::runtime_error when ComputeLocktime
        // finds no valid locktime (see src/pstt.cpp) -- not a bug.
        threw = true;
    }

    // GetIdentifier() throws iff its own internal ComputeLocktime call
    // returns false (src/pstt.cpp's `if (!ComputeLocktime(...)) throw`).
    // If this standalone call's result ever disagrees with whether
    // GetIdentifier() actually threw, that's a real ComputeLocktime
    // regression this fuzz target exists to catch -- not assert(), since
    // this project's default RelWithDebInfo build defines NDEBUG, which
    // would silently compile a plain assert() away.
    if (computed == threw) {
        abort();
    }
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_locktime(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_locktime(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
