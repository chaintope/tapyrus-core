// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <primitives/xfield.h>
#include <xfieldhistory.h>
#include <txdb.h>
#include <univalue.h>
#include <test_tapyrus.h>
#include <test_keys_helper.h>

/* 
 * As XFieldHistory is a global, the order of execution of test in this file seems to affect the results.
 * serialize tests behave differently depending on whether the 'add' test case executed before it or not.
 * To avoid this we use different test suits:
 * xfieldhistory_tests  - global map tests - add, remove, temp and serialize xfieldchange
 * xfield_tests  -  deserialize xfieldchange and CXField
 */

struct XFieldHistorySetup : public TestingSetup {
    XFieldHistorySetup() : TestingSetup(TAPYRUS_MODES::DEV) {
        pxFieldHistory->Reset();

        pxFieldHistory->Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[10]))), 20, uint256()));
        pxFieldHistory->Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[11]))), 40, uint256()));
        pxFieldHistory->Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[12]))), 60, uint256()));

        pxFieldHistory->Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(4000000, 30, uint256()));
        pxFieldHistory->Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(8000000, 50, uint256()));
        pxFieldHistory->Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(16000000, 70, uint256()));
    }
    ~XFieldHistorySetup() {
        pxFieldHistory->Reset();
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

    //test add
    history1.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[13]))), 70, uint256()));
    history1.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[14]))), 80, uint256()));

    //verify that the new history2 object uses the same map as history1
    BOOST_CHECK(&history1.getXFieldHistoryMap() == &history2.getXFieldHistoryMap());
    BOOST_CHECK_EQUAL(history1[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size(), history2[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size());
    BOOST_CHECK_EQUAL(history1[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].size(), history2[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].size());

    //verify that the tempHistory object is not affected by changes to history1(hisrory2)
    CTempXFieldHistory tempHistory1;
    history2.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[15]))), 80, uint256()));
    history2.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[0]))), 90, uint256()));

    BOOST_CHECK(tempHistory1[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size() != history2[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size());
    BOOST_CHECK(tempHistory1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).height != history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).height);

    //verify that changes in tempHistory1 object are not reflected in history1(hisrory2)
    tempHistory1.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[1]))), 100, uint256()));
    BOOST_CHECK(tempHistory1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 101).height != history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 101).height);

    tempHistory1.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(4000000, 91, uint256()));
    BOOST_CHECK(tempHistory1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 92).height != history1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 92).height);

    //verify that tempHistory is not affected
    BOOST_CHECK_EQUAL(tempHistory.getXFieldHistoryMap().size(), 2);
    BOOST_CHECK_EQUAL(tempHistory[TAPYRUS_XFIELDTYPES::AGGPUBKEY].size(), 4);
    BOOST_CHECK_EQUAL(tempHistory[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE].size(), 4);

    //xfield change serialize
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << history1[TAPYRUS_XFIELDTYPES::AGGPUBKEY][0];

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("21025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d300000000" + HexStr(FederationParams().GenesisBlock().GetHash())));

    stream.clear();
    history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).Serialize(stream);

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), "2103af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d5a0000000000000000000000000000000000000000000000000000000000000000000000");

    stream.clear();
    stream << history1.getXFieldHistoryMap().find(TAPYRUS_XFIELDTYPES::AGGPUBKEY)->second;

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("0821025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d300000000" + HexStr(FederationParams().GenesisBlock().GetHash()) + "2103831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc1400000000000000000000000000000000000000000000000000000000000000000000002102bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf2800000000000000000000000000000000000000000000000000000000000000000000002103b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b3c00000000000000000000000000000000000000000000000000000000000000000000002102b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a4600000000000000000000000000000000000000000000000000000000000000000000002102e78cafe033b22bda5d7d1c8e82ee932930bf12e08489bc19769cbec765568be95000000000000000000000000000000000000000000000000000000000000000000000002102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b5000000000000000000000000000000000000000000000000000000000000000000000002103af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d5a0000000000000000000000000000000000000000000000000000000000000000000000"));

    stream.clear();
    stream << history1[TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE][0];
    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()),
    std::string("40420f0000000000" + HexStr(FederationParams().GenesisBlock().GetHash())));

    stream.clear();
    stream << history1.getXFieldHistoryMap().find(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE)->second;

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("0440420f0000000000" + HexStr(FederationParams().GenesisBlock().GetHash()) + "00093d001e000000000000000000000000000000000000000000000000000000000000000000000000127a003200000000000000000000000000000000000000000000000000000000000000000000000024f400460000000000000000000000000000000000000000000000000000000000000000000000"));
}

BOOST_AUTO_TEST_SUITE_END()//xfieldhistory_tests
