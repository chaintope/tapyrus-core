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
 * xfield_tests  - serialize and deserialize xfieldchange anf CXField
 */

BOOST_FIXTURE_TEST_SUITE(xfield_tests, TestingSetup)

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


BOOST_AUTO_TEST_CASE(CXField_serialize)
{
    CXField xfieldAggPubKey(XFieldData(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[1])))));
    CXField xfieldMaxBlockSize(XFieldData(XFieldMaxBlockSize(2000)));

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << xfieldAggPubKey;
    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("012102ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e0527b900"));

    stream.clear();
    stream << xfieldMaxBlockSize;
    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("02d0070000"));

    xfieldMaxBlockSize.xfieldType = TAPYRUS_XFIELDTYPES::NONE;
    stream.clear();
    BOOST_CHECK_THROW(xfieldMaxBlockSize.Serialize(stream), BadXFieldException);

    xfieldAggPubKey.xfieldType = TAPYRUS_XFIELDTYPES::NONE;
    stream.clear();
    BOOST_CHECK_THROW(xfieldAggPubKey.Serialize(stream), BadXFieldException);

    xfieldMaxBlockSize.xfieldType = TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE;
    xfieldMaxBlockSize.xfieldValue = XFieldEmpty();
    BOOST_CHECK(!xfieldMaxBlockSize.IsValid());

}

BOOST_AUTO_TEST_CASE(CXField_unserialize)
{
    CXField xfieldAggPubKey;
    CXField xfieldMaxBlockSize;

    CDataStream stream(ParseHex("012102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> xfieldAggPubKey;

    BOOST_CHECK(xfieldAggPubKey.xfieldType == TAPYRUS_XFIELDTYPES::AGGPUBKEY);
    BOOST_CHECK(xfieldAggPubKey.xfieldValue == XFieldData(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[15])))));

    CDataStream stream2(ParseHex(std::string("02ffffffff"+ HexStr(FederationParams().GenesisBlock().GetHash()))), SER_NETWORK, PROTOCOL_VERSION);
    stream2 >> xfieldMaxBlockSize;
    
    BOOST_CHECK(xfieldMaxBlockSize.xfieldType == TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE);
    BOOST_CHECK(xfieldMaxBlockSize.xfieldValue == XFieldData(XFieldMaxBlockSize(0xffffffff)));

    CDataStream stream3(ParseHex(std::string("002102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"+ HexStr(FederationParams().GenesisBlock().GetHash()))), SER_NETWORK, PROTOCOL_VERSION);
    BOOST_CHECK_THROW(xfieldMaxBlockSize.Unserialize(stream3), BadXFieldException);

    CDataStream stream4(ParseHex(std::string("032102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"+ HexStr(FederationParams().GenesisBlock().GetHash()))), SER_NETWORK, PROTOCOL_VERSION);
    BOOST_CHECK_THROW(xfieldMaxBlockSize.Unserialize(stream4), BadXFieldException);

    CDataStream stream5(ParseHex(std::string("0300"+ HexStr(FederationParams().GenesisBlock().GetHash()))), SER_NETWORK, PROTOCOL_VERSION);
    BOOST_CHECK_THROW(xfieldMaxBlockSize.Unserialize(stream5), BadXFieldException);

    CDataStream stream7(ParseHex(std::string("032102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"+ HexStr(FederationParams().GenesisBlock().GetHash()))), SER_NETWORK, PROTOCOL_VERSION);
    BOOST_CHECK_THROW(xfieldAggPubKey.Unserialize(stream7), BadXFieldException);

}

BOOST_AUTO_TEST_SUITE_END()

