// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Fuzz target for IsMineInner (src/script/ismine.cpp), reached through its
// public wrapper IsMine(): classifies an arbitrary scriptPubKey against a
// keystore, recursing into P2SH redeem scripts. The keystore is populated
// from the front of the input (up to MAX_FUZZ_KEYS private keys,
// MAX_FUZZ_SCRIPTS redeem scripts and one optional watch-only script) so
// the ISMINE_SPENDABLE/ISMINE_WATCH_ONLY branches are reachable, not just
// ISMINE_NO; the remaining bytes are the scriptPubKey. No node context
// required.

#include <tapyrus-config.h>

#include <key.h>
#include <keystore.h>
#include <script/ismine.h>
#include <script/script.h>

#include <test/fuzz/fuzz_code/FuzzedDataProvider.h>

#include <cstdint>
#include <vector>
#include <unistd.h>

static constexpr uint8_t MAX_FUZZ_KEYS = 3;
static constexpr uint8_t MAX_FUZZ_SCRIPTS = 2;
static constexpr uint32_t PRIVATE_KEY_SIZE_BYTES = 32;
// One byte over the limit, so AddCScript()'s rejection path is reachable too.
static constexpr uint32_t MAX_FUZZ_SCRIPT_SIZE_BYTES = MAX_SCRIPT_ELEMENT_SIZE + 1;

// CKey::GetPubKey() (called by CBasicKeyStore::AddKey) needs the secp256k1
// signing context, which pstt_fuzz_driver.h's initialize() doesn't start.
class EccSigningContext
{
public:
    EccSigningContext() { ECC_Start(); }
    ~EccSigningContext() { ECC_Stop(); }
};

static CScript ConsumeScript(FuzzedDataProvider& fdp)
{
    const std::vector<uint8_t> bytes = fdp.ConsumeBytes<uint8_t>(
        fdp.ConsumeIntegralInRange<uint32_t>(0, MAX_FUZZ_SCRIPT_SIZE_BYTES));
    return CScript(bytes.begin(), bytes.end());
}

static int test_one_input_ismine(const uint8_t* data, size_t size)
{
    static const EccSigningContext ecc_signing_context;

    FuzzedDataProvider fdp(data, size);
    CBasicKeyStore keystore;

    const uint8_t key_count = fdp.ConsumeIntegralInRange<uint8_t>(0, MAX_FUZZ_KEYS);
    for (uint8_t i = 0; i < key_count; ++i) {
        const std::vector<uint8_t> secret = fdp.ConsumeBytes<uint8_t>(PRIVATE_KEY_SIZE_BYTES);
        CKey key;
        key.Set(secret.begin(), secret.end(), fdp.ConsumeBool());
        if (key.IsValid()) keystore.AddKey(key);
    }

    const uint8_t script_count = fdp.ConsumeIntegralInRange<uint8_t>(0, MAX_FUZZ_SCRIPTS);
    for (uint8_t i = 0; i < script_count; ++i) {
        // AddCScript() itself rejects oversized redeem scripts.
        keystore.AddCScript(ConsumeScript(fdp));
    }

    if (fdp.ConsumeBool()) keystore.AddWatchOnly(ConsumeScript(fdp));

    const std::vector<uint8_t> script_pub_key = fdp.ConsumeRemainingBytes<uint8_t>();
    IsMine(keystore, CScript(script_pub_key.begin(), script_pub_key.end()));
    return 0;
}

// Thin forwarder kept under the shared generic name for pstt_fuzz_driver.h's
// own main() (AFL/stdin path) to call -- see that header's comment for why
// LLVMFuzzerTestOneInput below must call the uniquely-named function
// directly instead of through this wrapper.
static int test_one_input(const uint8_t* data, size_t size)
{
    return test_one_input_ismine(data, size);
}

// This function is used by libFuzzer. Defined literally in this file
// (not in the shared pstt_fuzz_driver.h) so Fuzz Introspector's
// non-preprocessing, tree-sitter-based source analysis can correctly
// bind this call to this file's own test_one_input -- see
// pstt_fuzz_driver.h's own comment for why.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    test_one_input_ismine(data, size);
    return 0;
}

#include <test/fuzz/fuzz_code/pstt_fuzz_driver.h>
