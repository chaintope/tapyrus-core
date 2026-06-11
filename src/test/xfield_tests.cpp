// Copyright (c) 2019-2023 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <primitives/xfield.h>
#include <xfieldhistory.h>
#include <txdb.h>
#include <univalue.h>
#include <test/test_tapyrus.h>
#include <test/test_keys_helper.h>

/* 
 * xfield_tests  - serialize and deserialize xfieldchange anf CXField
 */

BOOST_FIXTURE_TEST_SUITE(xfield_tests, TestingSetup)


BOOST_AUTO_TEST_CASE(XFieldChange_deserialize)
{
    XFieldChangeListWrapper xfieldList(XFieldAggPubKey::BLOCKTREE_DB_KEY);

    CDataStream stream(ParseHex("012102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b000000000000000000000000000000000000000000000000000000000000000000000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> xfieldList;

    BOOST_CHECK(xfieldList.xfieldChanges.size() == 1);

    CPubKey pubkey(std::get<XFieldAggPubKey>(xfieldList.xfieldChanges[0].xfieldValue).data),
    pubkeyActual(ParseHex("02473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"));
    BOOST_CHECK(std::equal(pubkey.begin(), pubkey.end(), pubkeyActual.begin()));
    BOOST_CHECK(xfieldList.xfieldChanges[0].height == 0);
    BOOST_CHECK(xfieldList.xfieldChanges[0].blockHash == uint256());

    XFieldChangeListWrapper xfieldList2(XFieldMaxBlockSize::BLOCKTREE_DB_KEY);
    CDataStream stream2(ParseHex(std::string("01ffffffff00000000"+ HexStr(FederationParams().GenesisBlock().GetHash()))), SER_NETWORK, PROTOCOL_VERSION);
    stream2 >> xfieldList2;

    BOOST_CHECK(xfieldList2.xfieldChanges.size() == 1);
    uint32_t maxblocksize = std::get<XFieldMaxBlockSize>(xfieldList2.xfieldChanges[0].xfieldValue).data;

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

// BOOST_CHECK_EXCEPTION predicates to check the specific validation error
class HasReason {
public:
    HasReason(const std::string& reason) : m_reason(reason) {}
    bool operator() (const BadXFieldException& e) const {
        return std::string(e.what()).find(m_reason) != std::string::npos;
    };
private:
    const std::string m_reason;
};

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

    //incorrectly unserialized
    CXField xfield;
    CDataStream stream7(ParseHex(std::string("022102473757a955a23f75379820f3071abf5b3343b78eb54e52373d06259ffa6c550b"+ HexStr(FederationParams().GenesisBlock().GetHash()))), SER_NETWORK, PROTOCOL_VERSION);
    stream7 >> xfield;
    BOOST_CHECK(xfield.xfieldType == TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE);
    BOOST_CHECK(xfield.xfieldValue == XFieldData(XFieldMaxBlockSize(0x37470221)));

}

// XFieldAggPubKey::IsValid() must accept only 33-byte compressed keys (0x02/0x03 prefix).
// Uncompressed (0x04, 65 bytes) and hybrid (0x06/0x07, 65 bytes) encodings represent the
// same secp256k1 points but must be rejected to prevent xfieldHistory serialization aliasing
// and block-production halts caused by CPubKey::operator== being bytewise.
BOOST_AUTO_TEST_CASE(XFieldAggPubKey_IsValid_compressed_only)
{
    // Valid: compressed 0x02 prefix
    BOOST_CHECK(XFieldAggPubKey(ParseHex(ValidPubKeyStrings[1])).IsValid());  // 02ce7e...
    // Valid: compressed 0x03 prefix
    BOOST_CHECK(XFieldAggPubKey(ParseHex(ValidPubKeyStrings[0])).IsValid());  // 03af80...

    // Invalid: uncompressed encoding (65 bytes, 0x04 prefix) — same EC point, different bytes
    std::vector<unsigned char> uncompressed = ParseHex(UncompressedPubKeyString);
    BOOST_CHECK_EQUAL(uncompressed.size(), 65U);
    BOOST_CHECK(!XFieldAggPubKey(uncompressed).IsValid());

    // Invalid: hybrid-even encoding (65 bytes, 0x06 prefix)
    std::vector<unsigned char> hybridEven = uncompressed;
    hybridEven[0] = 0x06;
    BOOST_CHECK(!XFieldAggPubKey(hybridEven).IsValid());

    // Invalid: hybrid-odd encoding (65 bytes, 0x07 prefix)
    std::vector<unsigned char> hybridOdd = uncompressed;
    hybridOdd[0] = 0x07;
    BOOST_CHECK(!XFieldAggPubKey(hybridOdd).IsValid());

    // Invalid: empty data
    BOOST_CHECK(!XFieldAggPubKey().IsValid());

    // Invalid: 32 bytes (one byte short — raw scalar, no prefix)
    std::vector<unsigned char> key0 = ParseHex(ValidPubKeyStrings[0]);
    std::vector<unsigned char> tooShort(key0.begin() + 1, key0.end());
    BOOST_CHECK_EQUAL(tooShort.size(), 32U);
    BOOST_CHECK(!XFieldAggPubKey(tooShort).IsValid());

    // Invalid: 34 bytes (one byte too long)
    std::vector<unsigned char> tooLong = key0;
    tooLong.push_back(0x00);
    BOOST_CHECK_EQUAL(tooLong.size(), 34U);
    BOOST_CHECK(!XFieldAggPubKey(tooLong).IsValid());

    // Invalid: 33 bytes but 0x04 prefix (not a legal 65-byte uncompressed form)
    std::vector<unsigned char> badPrefix = ParseHex(ValidPubKeyStrings[0]);
    badPrefix[0] = 0x04;
    BOOST_CHECK(!XFieldAggPubKey(badPrefix).IsValid());

    // Invalid: 33 bytes but 0x00 prefix
    std::vector<unsigned char> zeroPrefix(33, 0x00);
    BOOST_CHECK(!XFieldAggPubKey(zeroPrefix).IsValid());

    // Invalid: 33 bytes with 0x02 prefix but payload is all-zeros (not on curve)
    std::vector<unsigned char> badPoint(33, 0x00);
    badPoint[0] = 0x02;
    BOOST_CHECK(!XFieldAggPubKey(badPoint).IsValid());

    // Full chain: CXField::IsValid() → XFieldValidityVisitor → XFieldAggPubKey::IsValid()
    // Uncompressed key in a block's xfield must be rejected at CheckBlockHeader.
    XFieldAggPubKey uncompressedXField{uncompressed};
    CXField xfieldUncompressed{XFieldData(uncompressedXField)};
    BOOST_CHECK(!xfieldUncompressed.IsValid());

    XFieldAggPubKey compressedXField{ParseHex(ValidPubKeyStrings[0])};
    CXField xfieldCompressed{XFieldData(compressedXField)};
    BOOST_CHECK(xfieldCompressed.IsValid());
}

BOOST_AUTO_TEST_SUITE_END()

