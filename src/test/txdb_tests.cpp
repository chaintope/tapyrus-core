// Copyright (c) 2019-2022 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txdb.h>
#include <test/test_tapyrus.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(txdb_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(txdb_aggpubkey_serialize_tests)
{
    XFieldAggpubkey xFieldAggpubkey;
    std::vector<XFieldAggpubkey> xFieldAggpubkeyList;
    CDataStream stream(ParseHex("0121025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d30a0000000000000000000000000000000000000000000000000000000000000000000000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> xFieldAggpubkeyList;

    // 1 item
    xFieldAggpubkey = xFieldAggpubkeyList[0];
    BOOST_CHECK_EQUAL(HexStr(xFieldAggpubkey.aggpubkey.begin(), xFieldAggpubkey.aggpubkey.begin()+xFieldAggpubkey.aggpubkey.size()), "025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3");
    BOOST_CHECK_EQUAL(xFieldAggpubkey.height, 10);
    BOOST_CHECK_EQUAL(xFieldAggpubkey.blockHash, uint256S("0000000000000000000000000000000000000000000000000000000000000000"));

    // 2 items
    CDataStream stream1(ParseHex("0221025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d30a00000000000000000000000000000000000000000000000000000000000000000000002103b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b00000000d39be6bcedc39831203a6ab531040f9ab30d1e66c1cbcd657c38b590d91e0da7"), SER_NETWORK, PROTOCOL_VERSION);
    stream1 >> xFieldAggpubkeyList;

    xFieldAggpubkey = xFieldAggpubkeyList[1];
    BOOST_CHECK_EQUAL(HexStr(xFieldAggpubkey.aggpubkey.begin(), xFieldAggpubkey.aggpubkey.begin()+xFieldAggpubkey.aggpubkey.size()), "03b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b");
    BOOST_CHECK_EQUAL(xFieldAggpubkey.height, 0);
    BOOST_CHECK_EQUAL(xFieldAggpubkey.blockHash, uint256S("a70d1ed990b5387c65cdcbc1661e0db39a0f0431b56a3a203198c3edbce69bd3"));

    // 5 items - error
    CDataStream stream2(ParseHex("0521025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d30a00000000000000000000000000000000000000000000000000000000000000000000002103b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b00000000d39be6bcedc39831203a6ab531040f9ab30d1e66c1cbcd657c38b590d91e0da7"), SER_NETWORK, PROTOCOL_VERSION);

    try {
        stream2 >> xFieldAggpubkeyList;
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    } catch (const std::ios_base::failure& e) {
    }

    // 5 items
    CDataStream stream3(ParseHex("05210205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a6200000000efdc019af254ce1b433c6f7b9e12a460b093e6b2f1030f6c4fa198ceb58304ee2102785a891f323acd6cef0fc509bb14304410595914267c50467e51c87142acbb5e0c0000009266cf8e354abefc42dc08582098da8c72ac70dcbcc388e1af02cba1cac057ef2102b9a609d6bec0fdc9ba690986013cf7bbd13c54ffc25e6cf30916b4732c4a952acb010000881ff5028b9e4af4b471e264a26ed0ea6e562d7c41f4d03ccb0d44abe42d88c12103831a69b8009833ab5b0326012eaf489bfea35a7321b1ca15b11d88131423fafc0a0a000083cfda6d9551908eddb9a22e0d25017c7bf79099ae2647a2eda8b475f3fbcfb921033e6e1d4ae3e7e1bc2173e2af1f2f65c6284ea7c6478f2241784c77b0dff98e61cba100009242e5b1aa02206e5123db3561e52b36f038d0564a99ae54799478fa3447a1eb"), SER_NETWORK, PROTOCOL_VERSION);

    stream3 >> xFieldAggpubkeyList;

    xFieldAggpubkey = xFieldAggpubkeyList[1];
    BOOST_CHECK_EQUAL(xFieldAggpubkeyList.size(), 5);

    BOOST_CHECK_EQUAL(HexStr(xFieldAggpubkeyList[0].aggpubkey.begin(), xFieldAggpubkeyList[0].aggpubkey.begin()+xFieldAggpubkeyList[0].aggpubkey.size()), "0205deb5ba6b1f7c22e79026f8301fe8d50e9e9af8514665c2440207e932d44a62");

    BOOST_CHECK_EQUAL(xFieldAggpubkeyList[0].height, 0);
    BOOST_CHECK_EQUAL(xFieldAggpubkeyList[1].height, 12);
    BOOST_CHECK_EQUAL(xFieldAggpubkeyList[2].height, 459);
    BOOST_CHECK_EQUAL(xFieldAggpubkeyList[3].height, 2570);
    BOOST_CHECK_EQUAL(xFieldAggpubkeyList[4].height, 41419);

    BOOST_CHECK_EQUAL(xFieldAggpubkeyList[4].blockHash, uint256S("eba14734fa78947954ae994a56d038f0362be56135db23516e2002aab1e54292"));

    // insufficient data
    CDataStream stream4(ParseHex("0221025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d30a00000000000000000000000000000000000000000000000000000000000000000000002103b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792"), SER_NETWORK, PROTOCOL_VERSION);
    try {
        stream4 >> xFieldAggpubkeyList;
        BOOST_CHECK_MESSAGE(false, "We should have thrown");
    } catch (const std::ios_base::failure& e) {
    }

    // extra data
    CDataStream stream5(ParseHex("0121025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d30a00000000000000000000000000000000000000000000000000000000000000000000002103b44f1cfcf46aba8bc98e2fd39f137cc43d98ab7792e4848b09c06198b042ca8b00000000d39be6bcedc39831203a6ab531040f9ab30d1e66c1cbcd657c38b590d91e0da700"), SER_NETWORK, PROTOCOL_VERSION);
    stream5 >> xFieldAggpubkeyList;
    BOOST_CHECK_EQUAL(xFieldAggpubkeyList.size(), 1);
}

BOOST_AUTO_TEST_CASE(txdb_aggpubkey_unserialize_tests)
{
    std::vector<XFieldAggpubkey> xFieldAggpubkeyList;
    XFieldAggpubkey xFieldAggpubkey;

    xFieldAggpubkey.aggpubkey = ParseHex("025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3");
    xFieldAggpubkey.height = 0;
    xFieldAggpubkey.blockHash = uint256S("a70d1ed990b5387c65cdcbc1661e0db39a0f0431b56a3a203198c3edbce69bd3");
    xFieldAggpubkeyList.push_back(xFieldAggpubkey);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << xFieldAggpubkeyList;
    BOOST_CHECK_EQUAL(xFieldAggpubkeyList.size(), 1);
    BOOST_CHECK_EQUAL(HexStr(stream.begin(), stream.end()), "0121025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d300000000d39be6bcedc39831203a6ab531040f9ab30d1e66c1cbcd657c38b590d91e0da7");

    xFieldAggpubkey.aggpubkey = ParseHex("023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e");
    xFieldAggpubkey.height = 100;
    xFieldAggpubkey.blockHash = uint256S("eba14734fa78947954ae994a56d038f0362be56135db23516e2002aab1e54292");
    xFieldAggpubkeyList.push_back(xFieldAggpubkey);

    CDataStream stream1(SER_NETWORK, PROTOCOL_VERSION);
    stream1 << xFieldAggpubkeyList;
    BOOST_CHECK_EQUAL(xFieldAggpubkeyList.size(), 2);
    BOOST_CHECK_EQUAL(HexStr(stream1.begin(), stream1.end()), "0221025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d300000000d39be6bcedc39831203a6ab531040f9ab30d1e66c1cbcd657c38b590d91e0da721023b435ce7b804aa66dcd65a855282479be5057fd82ce4c7c2e2430920de8b9e9e640000009242e5b1aa02206e5123db3561e52b36f038d0564a99ae54799478fa3447a1eb");

    xFieldAggpubkeyList.clear();
    int len = 0;
    for(int i = 0 ; i < 300; ++i)
    {
        xFieldAggpubkey.aggpubkey = ParseHex("025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3");
        xFieldAggpubkey.height = i;
        xFieldAggpubkey.blockHash = uint256S("a70d1ed990b5387c65cdcbc1661e0db39a0f0431b56a3a203198c3edbce69bd3");
        xFieldAggpubkeyList.push_back(xFieldAggpubkey);
        len += 140;
    }

    CDataStream stream2(SER_NETWORK, PROTOCOL_VERSION);
    stream2 << xFieldAggpubkeyList;
    BOOST_CHECK_EQUAL(HexStr(stream2.begin(), stream2.end()).size(), len + 6);
}

BOOST_AUTO_TEST_SUITE_END()