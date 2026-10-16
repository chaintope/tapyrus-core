// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <primitives/xfield.h>
#include <primitives/xfieldvalues.h>
#include <dynamicparams.h>
#include <xfieldhistory.h>
#include <xfieldhistorymap.h>
#include <tempxfieldhistory.h>
#include <txdb.h>
#include <univalue.h>
#include <test/test_tapyrus.h>
#include <test/test_keys_helper.h>

/*
 * xfieldhistory_tests  - add, remove, temp and serialize xfieldchange
 * xfield_tests  -  deserialize xfieldchange and CXField
 */

// XFieldHistorySetup builds its own history from this suite's genesis block
// plus synthetic entries to exercise Add/Remove/Get.
struct XFieldHistorySetup : public TestingSetup {
    CXFieldHistory m_history{DynamicParams().GenesisBlock()};

    XFieldHistorySetup() : TestingSetup(TAPYRUS_MODES::DEV) {
        m_history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[10]))), 20, uint256()));
        m_history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[11]))), 40, uint256()));
        m_history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[12]))), 60, uint256()));

        m_history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(4000000, 30, uint256()));
        m_history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(8000000, 50, uint256()));
        m_history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(16000000, 70, uint256()));
    }
};

// A copy of a history that also exposes its map, for the size and serialization checks.
struct InspectableXFieldHistory : public CTempXFieldHistory {
    explicit InspectableXFieldHistory(const CXFieldHistoryMap& history) : CTempXFieldHistory(history) {}
    const XFieldHistoryMapType& Map() const { return xfieldHistory; }
};

//xfieldhistory_tests
BOOST_FIXTURE_TEST_SUITE(xfieldhistory_tests, XFieldHistorySetup)

BOOST_AUTO_TEST_CASE(xfieldhistory_size_and_temp) 
{
    CXFieldHistory& history1 = m_history;
    InspectableXFieldHistory tempHistory(history1);

    //verify that tempHistory is initialized correctly
    BOOST_CHECK_EQUAL(tempHistory.Map().size(), 2);
    BOOST_CHECK_EQUAL(tempHistory.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 4);
    BOOST_CHECK_EQUAL(tempHistory.GetListSize(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE), 4);

    BOOST_CHECK_EQUAL(tempHistory.Map().size(), InspectableXFieldHistory(history1).Map().size());
    BOOST_CHECK_EQUAL(tempHistory.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), history1.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY));
    BOOST_CHECK_EQUAL(tempHistory.GetListSize(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE), history1.GetListSize(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE));

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

    //verify that the tempHistory1 object is not affected by changes to history1
    CTempXFieldHistory tempHistory1(history1);
    history1.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[15]))), 80, uint256()));
    history1.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[0]))), 90, uint256()));

    BOOST_CHECK(tempHistory1.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY) != history1.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY));
    BOOST_CHECK(tempHistory1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).height != history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).height);

    //verify that changes in tempHistory1 object are not reflected in history1
    tempHistory1.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[1]))), 100, uint256()));
    BOOST_CHECK(tempHistory1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 101).height != history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 101).height);

    tempHistory1.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(4000000, 91, uint256()));
    BOOST_CHECK(tempHistory1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 92).height != history1.Get(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, 92).height);

    //verify that tempHistory is not affected
    BOOST_CHECK_EQUAL(tempHistory.Map().size(), 2);
    BOOST_CHECK_EQUAL(tempHistory.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 4);
    BOOST_CHECK_EQUAL(tempHistory.GetListSize(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE), 4);

    //xfield change serialize
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << history1.GetListCopy(TAPYRUS_XFIELDTYPES::AGGPUBKEY).at(0);

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("21025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d300000000" + HexStr(DynamicParams().GenesisBlock().GetHash())));

    stream.clear();
    history1.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 91).Serialize(stream);

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), "2103af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d5a0000000000000000000000000000000000000000000000000000000000000000000000");

    stream.clear();
    stream << InspectableXFieldHistory(history1).Map().find(TAPYRUS_XFIELDTYPES::AGGPUBKEY)->second;

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("0821025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d300000000" + HexStr(DynamicParams().GenesisBlock().GetHash()) + "2103831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc1400000000000000000000000000000000000000000000000000000000000000000000002102bf2027c8455800c7626542219e6208b5fe787483689f1391d6d443ec85673ecf2800000000000000000000000000000000000000000000000000000000000000000000002103b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b3c00000000000000000000000000000000000000000000000000000000000000000000002102b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952a4600000000000000000000000000000000000000000000000000000000000000000000002102e78cafe033b22bda5d7d1c8e82ee932930bf12e08489bc19769cbec765568be95000000000000000000000000000000000000000000000000000000000000000000000002102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b5000000000000000000000000000000000000000000000000000000000000000000000002103af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d5a0000000000000000000000000000000000000000000000000000000000000000000000"));

    stream.clear();
    stream << history1.GetListCopy(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE).at(0);
    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()),
    std::string("40420f0000000000" + HexStr(DynamicParams().GenesisBlock().GetHash())));

    stream.clear();
    stream << InspectableXFieldHistory(history1).Map().find(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE)->second;

    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), std::string("0440420f0000000000" + HexStr(DynamicParams().GenesisBlock().GetHash()) + "00093d001e000000000000000000000000000000000000000000000000000000000000000000000000127a003200000000000000000000000000000000000000000000000000000000000000000000000024f400460000000000000000000000000000000000000000000000000000000000000000000000"));
}

BOOST_AUTO_TEST_CASE(xfieldhistory_remove_and_reorg)
{
    // Fixture: genesis(key0@0) + key10@20 + key11@40 + key12@60 = 4 entries
    BOOST_CHECK_EQUAL(m_history.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 4);

    // ConnectTip: rotate to key[13] at height 80
    const uint256 hashA = InsecureRand256();
    const XFieldChange changeA(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[13]))), 80, hashA);
    m_history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, changeA);
    BOOST_CHECK_EQUAL(m_history.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 5);
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 80).blockHash, hashA);

    // DisconnectTip: Remove that rotation entry; reverts to previous entry at height 60
    BOOST_CHECK(m_history.Remove(TAPYRUS_XFIELDTYPES::AGGPUBKEY, changeA));
    BOOST_CHECK_EQUAL(m_history.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 4);
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 80).height, 60);

    // Removing a non-existent entry is a safe no-op
    BOOST_CHECK(!m_history.Remove(TAPYRUS_XFIELDTYPES::AGGPUBKEY, changeA));

    // ConnectTip on new chain: different key at the same height
    const uint256 hashB = InsecureRand256();
    const XFieldChange changeB(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[14]))), 80, hashB);
    m_history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, changeB);
    BOOST_CHECK_EQUAL(m_history.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 5);
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 80).blockHash, hashB);
    // Earlier entries are unchanged
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 60).height, 60);
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 40).height, 40);
}

BOOST_AUTO_TEST_CASE(xfieldhistory_same_height_active_chain_wins)
{
    // Fixture: genesis(key0@0) + key10@20 + key11@40 + key12@60 = 4 entries
    BOOST_CHECK_EQUAL(m_history.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 4);

    // Orphan entry added FIRST (as would happen during reindex via LoadExternalBlockFile)
    const uint256 hashOrphan = InsecureRand256();
    const XFieldChange orphanChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[13]))), 80, hashOrphan);
    m_history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, orphanChange);

    // Active-chain entry added SECOND (from ConnectTip after ActivateBestChain)
    const uint256 hashActive = InsecureRand256();
    const XFieldChange activeChange(XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[14]))), 80, hashActive);
    m_history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, activeChange);
    BOOST_CHECK_EQUAL(m_history.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 6);

    // Get(height) scans from the back: the most recently added (active-chain) entry wins
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 80).blockHash, hashActive);
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 85).blockHash, hashActive);
    // Height below both entries still resolves to the previous rotation at 60
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 79).height, 60);

    // After removing the active entry, the orphan entry becomes the last
    BOOST_CHECK(m_history.Remove(TAPYRUS_XFIELDTYPES::AGGPUBKEY, activeChange));
    BOOST_CHECK_EQUAL(m_history.GetListSize(TAPYRUS_XFIELDTYPES::AGGPUBKEY), 5);
    BOOST_CHECK_EQUAL(m_history.Get(TAPYRUS_XFIELDTYPES::AGGPUBKEY, 80).blockHash, hashOrphan);
}

// IsXFieldNew uses Get(nHeight) — the block's own effective height — so that
// a block immediately following a rotation is not misidentified as "new".
BOOST_AUTO_TEST_CASE(is_xfield_new_consecutive_same_value)
{
    // Fixture: genesis(key0@0) + key10@20 + key11@40 + key12@60 = 4 entries.
    // key12 was stored with effective height 60 (block at h=59 introduced it).

    CXFieldHistory& history = m_history;

    // (1) A block at height 60 whose xfield equals key12 must NOT be treated as
    //     new: Get(60) returns the key12 entry (60 <= 60), value matches.
    CXField sameAsLatest;
    sameAsLatest.xfieldType = TAPYRUS_XFIELDTYPES::AGGPUBKEY;
    sameAsLatest.xfieldValue = XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[12])));
    BOOST_CHECK(!IsXFieldNew(sameAsLatest, &history, 60));

    // (2) A block at height 61 (the block right after the rotation) with the
    //     same value must also NOT be treated as new.
    BOOST_CHECK(!IsXFieldNew(sameAsLatest, &history, 61));

    // (3) A block at height 61 that reverts to key11 (the pre-rotation value)
    //     MUST be treated as new: it is a genuine rotation away from key12.
    CXField revertToPrev;
    revertToPrev.xfieldType = TAPYRUS_XFIELDTYPES::AGGPUBKEY;
    revertToPrev.xfieldValue = XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[11])));
    BOOST_CHECK(IsXFieldNew(revertToPrev, &history, 61));

    // (4) A genuinely new key at any height is correctly identified as new.
    CXField brandNew;
    brandNew.xfieldType = TAPYRUS_XFIELDTYPES::AGGPUBKEY;
    brandNew.xfieldValue = XFieldAggPubKey(CPubKey(ParseHex(ValidPubKeyStrings[13])));
    BOOST_CHECK(IsXFieldNew(brandNew, &history, 65));
}

BOOST_AUTO_TEST_SUITE_END()//xfieldhistory_tests
