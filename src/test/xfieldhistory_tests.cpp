// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <primitives/xfield.h>
#include <xfieldhistory.h>
#include <txdb.h>
#include <univalue.h>
#include "test_tapyrus.h"
#include "test_keys_helper.h"

/* 
 * As XFieldHistory is a global, the order of execution of test in this file seems to affect the results.
 * serialize tests behave differently depending on whether the 'add' test case executed before it or not.
 * To avoid this we use different test suits:
 * xfieldhistory_tests  - global map tests - add, remove, temp etc
 * xfieldchange_tests  - serialize and deserialize
 */

//xfieldchange_tests 
BOOST_FIXTURE_TEST_SUITE(xfieldchange_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(XFieldChange_serialize)
{
    CXFieldHistory history1;

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << history1[TAPYRUS_XFIELDTYPES::AGGPUBKEY][0];

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("21025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d300000000" + HexStr(FederationParams().GenesisBlock().GetHash())));

    CXFieldHistory history;
    history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[10]))), 30, uint256()));
    history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[11]))), 60, uint256()));
    history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[12]))), 90, uint256()));

    stream.clear();
    history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).Serialize(stream);

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), "2103b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b5a0000000000000000000000000000000000000000000000000000000000000000000000");

    stream.clear();
    stream << history1.getXFieldHistoryMap().find(TAPYRUS_XFIELDTYPES::AGGPUBKEY)->second;

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("0421025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d300000000" + HexStr(FederationParams().GenesisBlock().GetHash()) + 
    "2103831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc1e00000000000000000000000000000000000000000000000000000000000000000000002102bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf3c00000000000000000000000000000000000000000000000000000000000000000000002103b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b5a0000000000000000000000000000000000000000000000000000000000000000000000"));

    stream.clear();
    stream << history1[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE][0];
    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()),
    std::string("40420f0000000000" + HexStr(FederationParams().GenesisBlock().GetHash())));

    history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(4000000, 30, uint256()));
    history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(8000000, 50, uint256()));
    history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(16000000, 70, uint256()));

    stream.clear();
    stream << history1.getXFieldHistoryMap().find(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE)->second;

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("0440420f0000000000" + HexStr(FederationParams().GenesisBlock().GetHash()) + "00093d001e000000000000000000000000000000000000000000000000000000000000000000000000127a003200000000000000000000000000000000000000000000000000000000000000000000000024f400460000000000000000000000000000000000000000000000000000000000000000000000"));
}

BOOST_AUTO_TEST_CASE(XFieldChange_deserialize)
{
    XFieldChangeListWrapper xfieldList(XFieldAggPubKey::BLOCKTREE_DB_KEY);

    CDataStream stream(ParseHex("012102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b000000000000000000000000000000000000000000000000000000000000000000000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> xfieldList;

    BOOST_CHECK(xfieldList.xfieldChanges.size() == 1);

    CPubKey pubkey(boost::get<XFieldAggPubKey>(xfieldList.xfieldChanges[0].xfieldValue).data),
    pubkeyActual(ParseHex("02473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"));
    BOOST_CHECK(std::equal(pubkey.begin(), pubkey.end(), pubkeyActual.begin()));
    BOOST_CHECK(xfieldList.xfieldChanges[0].height == 0);
    BOOST_CHECK(xfieldList.xfieldChanges[0].blockHash == uint256());

    XFieldChangeListWrapper xfieldList2(XFieldMaxBlockSize::BLOCKTREE_DB_KEY);
    CDataStream stream2(ParseHex(std::string("01ffffffff00000000"+ HexStr(FederationParams().GenesisBlock().GetHash()))), SER_NETWORK, PROTOCOL_VERSION);
    stream2 >> xfieldList2;

    BOOST_CHECK(xfieldList2.xfieldChanges.size() == 1);
    uint32_t maxblocksize = boost::get<XFieldMaxBlockSize>(xfieldList2.xfieldChanges[0].xfieldValue).data;

    BOOST_CHECK(maxblocksize == 0xffffffff);
    BOOST_CHECK(xfieldList2.xfieldChanges[0].height == 0);
    BOOST_CHECK(xfieldList2.xfieldChanges[0].blockHash == FederationParams().GenesisBlock().GetHash());
}

BOOST_AUTO_TEST_SUITE_END()//xfieldchange_tests

struct XFieldHistorySetup : public TestingSetup {
    XFieldHistorySetup() : TestingSetup(TAPYRUS_MODES::DEV) {
        CXFieldHistory history;
        history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[10]))), 20, uint256()));
        history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[11]))), 40, uint256()));
        history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[12]))), 60, uint256()));

        history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(4000000, 30, uint256()));
        history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(8000000, 50, uint256()));
        history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(16000000, 70, uint256()));
    }
};

//xfieldhistory_tests
BOOST_FIXTURE_TEST_SUITE(xfieldhistory_tests, XFieldHistorySetup)

BOOST_AUTO_TEST_CASE(xfieldhistory_size_and_temp) 
{
    CXFieldHistory history1, history2;
    CTempXFieldHistory tempHistory;

    //verify that history1 and history2 share a map.
    BOOST_CHECK(&history1.getXFieldHistoryMap() == &history2.getXFieldHistoryMap());

    //but tempHistory uses a different map
    BOOST_CHECK(&tempHistory.getXFieldHistoryMap() != &history1.getXFieldHistoryMap());
    BOOST_CHECK(&tempHistory.getXFieldHistoryMap() != &history2.getXFieldHistoryMap());

    //verify that tempHistory is initialized correctly
    BOOST_CHECK_EQUAL(tempHistory.getXFieldHistoryMap().size(), 2);
    BOOST_CHECK_EQUAL(tempHistory[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size(), 4);
    BOOST_CHECK_EQUAL(tempHistory[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].size(), 4);

    BOOST_CHECK_EQUAL(tempHistory.getXFieldHistoryMap().size(), history1.getXFieldHistoryMap().size());
    BOOST_CHECK_EQUAL(tempHistory[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size(), history1[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size());
    BOOST_CHECK_EQUAL(tempHistory[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].size(), history1[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].size());

    //verify access to different elements in the map and xfield change list
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 0).height, history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 0).height);
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 1).height, history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 1).height);
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 40).height, history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 40).height);
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 55).height, history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 55).height);
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 75).height, history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 75).height);

    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 0).height, history1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 0).height);
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 1).height, history1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 1).height);
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 55).height, history1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 55).height);
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 60).height, history1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 60).height);
    BOOST_CHECK_EQUAL(tempHistory.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 70).height, history1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 70).height);
}

BOOST_AUTO_TEST_CASE(xfieldhistory_add)
{
    CXFieldHistory history1;
    history1.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[13]))), 70, uint256()));
    history1.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[14]))), 80, uint256()));

    //verify that the new history2 object uses the same map as history1
    CXFieldHistory history2;
    BOOST_CHECK(&history1.getXFieldHistoryMap() == &history2.getXFieldHistoryMap());
    BOOST_CHECK_EQUAL(history1[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size(), history2[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size());
    BOOST_CHECK_EQUAL(history1[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].size(), history2[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].size());

    //verify that the tempHistory object is not affected by changes to history1(hisrory2)
    CTempXFieldHistory tempHistory;
    history2.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[15]))), 80, uint256()));
    history2.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[0]))), 90, uint256()));

    BOOST_CHECK(tempHistory[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size() != history2[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size());
    BOOST_CHECK(tempHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).height != history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).height);

    //verify that changes in tempHistory object are not reflected in history1(hisrory2)
    tempHistory.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[1]))), 100, uint256()));
    BOOST_CHECK(tempHistory.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 101).height != history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 101).height);

    tempHistory.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(4000000, 91, uint256()));
    BOOST_CHECK(tempHistory.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 92).height != history1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 92).height);
}

BOOST_AUTO_TEST_SUITE_END()//xfieldhistory_tests
