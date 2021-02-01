// Copyright (c) 2021 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>
#include <wallet/coincontrol.h>
#include <vector>
#include <set>
#include <boost/test/unit_test.hpp>
#include <validation.h>
#include <wallet/test/wallet_test_fixture.h>
#include <test/test_tapyrus.h>
#include <consensus/validation.h>

BOOST_FIXTURE_TEST_SUITE(create_transaction_tests, WalletTestingSetup)

BOOST_FIXTURE_TEST_CASE(test_create_transaction, TestWalletSetup)
{
    std::vector<unsigned char> vchPubkey = ParseHex("03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640");
    CPubKey pubkey(vchPubkey.begin(), vchPubkey.end());

    ImportCoin(10 * COIN);

    // Create a tx that sends TPC to key1 from key0
    CCoinControl coinControl;
    CReserveKey reservekey(wallet.get());
    CAmount nFeeRequired;
    std::string strError;
    int nChangePosRet = -1;
    std::vector<CRecipient> vecSend;
    CTxDestination dest { pubkey.GetID() };
    CScript scriptPubKey = GetScriptForDestination(dest);
    CRecipient recipient = {scriptPubKey, 100 * CENT, false};
    vecSend.push_back(recipient);
    CTransactionRef tx;
    BOOST_CHECK(wallet->CreateTransaction(vecSend, tx, reservekey, nFeeRequired, nChangePosRet, strError, coinControl));
    BOOST_CHECK_EQUAL(strError.size(), 0);
    BOOST_CHECK_EQUAL(tx->vout.size(), 2);

    // Try to set recipient who receives colored coin, fSubtractFeeFromAmount as true, and
    // it should fail.
    ColorIdentifier cid;
    CDataStream stream(ParseHex("c1f335bd3240ddfd87a2c2fc5a53210606460f19143f5e475729c46e06fcc9858f"), SER_NETWORK, INIT_PROTO_VERSION);
    stream >> cid;
    vecSend.clear();
    vecSend.push_back({GetScriptForDestination({ pubkey.GetID() }, cid), 100 * CENT, true});
    BOOST_CHECK(!wallet->CreateTransaction(vecSend, tx, reservekey, nFeeRequired, nChangePosRet, strError, coinControl));
    BOOST_CHECK_EQUAL(strError, "Recipient that receives colored coin must not be a target of subtract fee");
}

BOOST_FIXTURE_TEST_CASE(test_creating_colored_transaction, TestWalletSetup)
{
    std::vector<unsigned char> vchPubkey = ParseHex("03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640");
    CPubKey pubkey(vchPubkey.begin(), vchPubkey.end());

    ImportCoin(10 * COIN);

    ColorIdentifier cid;
    BOOST_CHECK(IssueNonReissunableColoredCoin(100 * CENT, cid));
    BOOST_CHECK_EQUAL(wallet->GetBalance()[cid], 100 * CENT);

    // Create a tx that sends colored coin to the pubkey
    CCoinControl coinControl;
    CReserveKey reservekey(wallet.get());
    CAmount nFeeRequired;
    std::string strError;
    int nChangePosRet = -1;
    std::vector<CRecipient> vecSend;
    CScript scriptpubkey = GetScriptForDestination({ pubkey.GetID() }, cid);
    CRecipient recipient = {scriptpubkey, 100 * CENT, false};
    vecSend.push_back(recipient);
    CTransactionRef tx;
    BOOST_CHECK(wallet->CreateTransaction(vecSend, tx, reservekey, nFeeRequired, nChangePosRet, strError, coinControl));
    BOOST_CHECK_EQUAL(strError.size(), 0);
    BOOST_CHECK_EQUAL(tx->vout.size(), 2);

    // The one of outputs should be payment to counterparty.
    auto i = std::find_if(tx->vout.begin(), tx->vout.end(), [&]( const CTxOut &out ) {
        return out.scriptPubKey == scriptpubkey;
    }) - tx->vout.begin();
    BOOST_CHECK_EQUAL(tx->vout[i].nValue, 100 * CENT);

    // The another one should be change of paying fee by TPC.
    auto j = std::find_if(tx->vout.begin(), tx->vout.end(), [&]( const CTxOut &out ) {
        return out.scriptPubKey != scriptpubkey;
    }) - tx->vout.begin();
    BOOST_CHECK(wallet->IsMine(tx->vout[j]));
    BOOST_CHECK(!tx->vout[j].scriptPubKey.IsColoredScript());

    // tx should be acceptable to a block.
    BOOST_CHECK(AddToWalletAndMempool(tx));
    BOOST_CHECK(ProcessBlockAndScanForWalletTxns(tx));

    BOOST_CHECK_EQUAL(wallet->GetBalance()[cid], 0);
}

BOOST_AUTO_TEST_SUITE_END()