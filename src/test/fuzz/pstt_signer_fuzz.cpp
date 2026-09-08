// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for SignPSTTInput (src/pstt.cpp), the Signer role's core --
// and, run against the keyless DUMMY_SIGNING_PROVIDER as done here, the
// exact same dry-run call doc/tapyrus/pstt.md documents finalizepstt and
// decodepstt's "next" field using to check role-completeness without
// fabricating new signatures ("Role completeness is determined by the
// same check finalizepstt itself uses (a dry-run SignPSTTInput against a
// keyless provider..."). One harness therefore genuinely covers both the
// Signer role and that pre-finalize completeness check, rather than
// needing a second target for the latter.
//
// A keyless provider can never actually add a signature (GetKey always
// returns false), so this exercises SignPSTTInput's UTXO/prevout/
// sighash-conflict/already-finalized bookkeeping -- the same checks that
// run on every real signing attempt -- without needing to construct real
// keys or valid signatures to reach them.

#include <tapyrus-config.h>

#include <core_io.h>
#include <key.h>
#include <pstt.h>
#include <pubkey.h>
#include <script/sign.h>

#include <test/fuzz/FuzzedDataProvider.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

static bool read_stdin(std::vector<uint8_t>& data)
{
    uint8_t buffer[1024];
    ssize_t length = 0;
    while ((length = read(STDIN_FILENO, buffer, 1024)) > 0) {
        data.insert(data.end(), buffer, buffer + length);
        if (data.size() > (1 << 20)) return false;
    }
    return length == 0;
}

static int test_one_input(const uint8_t* data, size_t size)
{
    FuzzedDataProvider fdp(data, size);

    // index/sighash/sigScheme steer which of SignPSTTInput's own branches
    // run; the remainder of the buffer is the base64 PSTT text, since
    // that's the field with actual parsing depth.
    const unsigned int index = fdp.ConsumeIntegral<unsigned int>();
    const int sighash = fdp.ConsumeIntegral<int>();
    const SignatureScheme sigScheme =
        fdp.ConsumeBool() ? SignatureScheme::SCHNORR : SignatureScheme::ECDSA;
    const std::string base64_pstt = fdp.ConsumeRemainingBytesAsString();

    PartiallySignedTapyrusTransaction pstt;
    std::string error;
    if (!DecodePSTT(pstt, base64_pstt, error)) return 0;

    SignatureData sigdata;
    try {
        SignPSTTInput(DUMMY_SIGNING_PROVIDER, pstt, index, sigdata, sighash, sigScheme);
    } catch (const std::exception&) {
        // pstt.inputs.at(index) throws std::out_of_range for an
        // out-of-bounds index (see src/pstt.cpp) -- not a bug.
    }
    return 0;
}

static std::unique_ptr<ECCVerifyHandle> globalVerifyHandle;
void initialize()
{
    globalVerifyHandle = std::make_unique<ECCVerifyHandle>();
}

// This function is used by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input(data, size);
    return 0;
}

// This function is used by libFuzzer.
extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    initialize();
    return 0;
}

// Disabled under WIN32 due to clash with Cygwin's WinMain.
#ifndef WIN32
// Declare main(...) "weak" to allow for libFuzzer linking. libFuzzer provides
// the main(...) function.
__attribute__((weak))
#endif
int main(int argc, char** argv)
{
    initialize();
#ifdef __AFL_INIT
    // Enable AFL deferred forkserver mode. Requires compilation using
    // afl-clang-fast++. See doc/fuzzing.md for details.
    __AFL_INIT();
#endif

#ifdef __AFL_LOOP
    // Enable AFL persistent mode. Requires compilation using afl-clang-fast++.
    // See doc/fuzzing.md for details.
    int ret = 0;
    while (__AFL_LOOP(1000)) {
        std::vector<uint8_t> buffer;
        if (!read_stdin(buffer)) continue;
        ret = test_one_input(buffer.data(), buffer.size());
    }
    return ret;
#else
    std::vector<uint8_t> buffer;
    if (!read_stdin(buffer)) return 0;
    return test_one_input(buffer.data(), buffer.size());
#endif
}
