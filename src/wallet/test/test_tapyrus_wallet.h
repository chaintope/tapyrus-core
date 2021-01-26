// Copyright (c) 2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TAPYRUS_TEST_TAPYRUS_WALLET_H
#define TAPYRUS_TEST_TAPYRUS_WALLET_H

#include <test/test_tapyrus.h>
#include <wallet/wallet.h>

class TestWalletSetup : public TestChainSetup {
public:
    TestWalletSetup(): TestChainSetup() {
        initWallet();
    }

    ~TestWalletSetup() {
        wallet.reset();
    }

    std::unique_ptr<CWallet> wallet;

    bool ImportCoin(const CAmount amount);
    bool IssueNonReissunableColoredCoin(const CAmount amount, ColorIdentifier& cid);
    bool ProcessBlockAndScanForWalletTxns(const CTransactionRef tx);
    bool AddToWalletAndMempool(const CTransactionRef tx);
private:
    void initWallet();
    void Sign(std::vector<unsigned char>& vchSig, CKey& signKey, const CScript& scriptPubKey, int inIndex, CMutableTransaction& outTx, int outIndex);
};

#endif //TAPYRUS_TEST_TAPYRUS_WALLET_H
