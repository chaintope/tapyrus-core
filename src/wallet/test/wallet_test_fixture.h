// Copyright (c) 2016-2018 The Bitcoin Core developers
// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_TEST_WALLET_TEST_FIXTURE_H
#define BITCOIN_WALLET_TEST_WALLET_TEST_FIXTURE_H

#include <test/test_tapyrus.h>

#include <wallet/wallet.h>

#include <memory>

/** Testing setup and teardown for wallet.
 */
struct WalletTestingSetup: public TestingSetup {
    explicit WalletTestingSetup(const std::string& chainName = TAPYRUS_MODES::PROD);
    ~WalletTestingSetup();

    CWallet m_wallet;
};

#endif // BITCOIN_WALLET_TEST_WALLET_TEST_FIXTURE_H
