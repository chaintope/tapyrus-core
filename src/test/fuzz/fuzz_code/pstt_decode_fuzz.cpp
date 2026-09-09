// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for DecodePSTT (src/core_read.cpp), the entry point behind
// decodepstt/walletprocesspstt/combinepstt etc: base64-decodes an
// RPC-client-controlled string, then unserializes the resulting bytes as
// a PartiallySignedTapyrusTransaction (src/pstt.h/.cpp), which enforces
// IsSane() unconditionally at the end of parsing. Complements
// pstt_parse_fuzz.cpp (which fuzzes the JSON "inputs" argument shape
// instead) by exercising the wire-format parser directly -- the same
// surface rpc_pstt.py's make_bare_pstt_with_unknown_global_field/
// make_pstt_output_missing_amount/make_pstt_out_of_range_prevout_index
// hand-craft individual cases for.
//
// DecodePSTT itself catches std::exception internally and never throws
// to its caller (confirmed by reading src/core_read.cpp), so this target
// takes the raw fuzz buffer as the base64 string directly -- no
// try/catch needed here, unlike pstt_parse_fuzz.cpp's JSON layer.

#include <tapyrus-config.h>

#include <core_io.h>
#include <key.h>
#include <pstt.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

static int test_one_input(const uint8_t* data, size_t size)
{
    const std::string base64_pstt(reinterpret_cast<const char*>(data), size);

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    DecodePSTT(pstt, base64_pstt, error);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
