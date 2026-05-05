// Copyright (c) 2012-2018 The Bitcoin Core developers
// Copyright (c) 2019-2025 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fs.h>
#include <streams.h>
#include <support/allocators/zeroafterfree.h>
#include <test/test_tapyrus.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(streams_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(streams_vector_writer)
{
    unsigned char a(1);
    unsigned char b(2);
    unsigned char bytes[] = { 3, 4, 5, 6 };
    std::vector<unsigned char> vch;

    // Each test runs twice. Serializing a second time at the same starting
    // point should yield the same results, even if the first test grew the
    // vector.

    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 0, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{1, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 0, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{1, 2}}));
    vch.clear();

    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 1, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 1, 2}}));
    vch.clear();

    vch.resize(5, 0);
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 1, 2, 0}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 1, 2, 0}}));
    vch.clear();

    vch.resize(4, 0);
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 3, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 0, 1, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 3, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 0, 1, 2}}));
    vch.clear();

    vch.resize(4, 0);
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 4, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 0, 0, 1, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 4, a, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{0, 0, 0, 0, 1, 2}}));
    vch.clear();

    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 0, bytes);
    BOOST_CHECK((vch == std::vector<unsigned char>{{3, 4, 5, 6}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 0, bytes);
    BOOST_CHECK((vch == std::vector<unsigned char>{{3, 4, 5, 6}}));
    vch.clear();

    vch.resize(4, 8);
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, bytes, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{8, 8, 1, 3, 4, 5, 6, 2}}));
    CVectorWriter(SER_NETWORK, INIT_PROTO_VERSION, vch, 2, a, bytes, b);
    BOOST_CHECK((vch == std::vector<unsigned char>{{8, 8, 1, 3, 4, 5, 6, 2}}));
    vch.clear();
}

BOOST_AUTO_TEST_CASE(streams_serializedata_xor)
{
    std::vector<char> in;
    std::vector<char> expected_xor;
    std::vector<unsigned char> key;
    CDataStream ds(in, 0, 0);

    // Degenerate case

    key.push_back('\x00');
    key.push_back('\x00');
    ds.Xor(key);
    BOOST_CHECK_EQUAL(
            std::string(expected_xor.begin(), expected_xor.end()),
            std::string(ds.begin(), ds.end()));

    in.push_back('\x0f');
    in.push_back('\xf0');
    expected_xor.push_back('\xf0');
    expected_xor.push_back('\x0f');

    // Single character key

    ds.clear();
    ds.insert(ds.begin(), in.begin(), in.end());
    key.clear();

    key.push_back('\xff');
    ds.Xor(key);
    BOOST_CHECK_EQUAL(
            std::string(expected_xor.begin(), expected_xor.end()),
            std::string(ds.begin(), ds.end()));

    // Multi character key

    in.clear();
    expected_xor.clear();
    in.push_back('\xf0');
    in.push_back('\x0f');
    expected_xor.push_back('\x0f');
    expected_xor.push_back('\x00');

    ds.clear();
    ds.insert(ds.begin(), in.begin(), in.end());

    key.clear();
    key.push_back('\xff');
    key.push_back('\x0f');

    ds.Xor(key);
    BOOST_CHECK_EQUAL(
            std::string(expected_xor.begin(), expected_xor.end()),
            std::string(ds.begin(), ds.end()));
}

BOOST_AUTO_TEST_CASE(buffered_writer_reader_round_trip)
{
    const uint32_t v1 = 0x12345678, v2 = 0xDEADBEEF, v3 = 0xCAFEBABE;
    const fs::path test_file = GetDataDir() / "test_buffered_write_read.bin";

    // Write via BufferedWriter with a 2-value-sized buffer to exercise flushing
    {
        CAutoFile file(fsbridge::fopen(test_file, "w+b"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!file.IsNull());
        BufferedWriter<CAutoFile> writer(file, sizeof(v1) + sizeof(v2));
        writer << v1 << v2 << v3;
    }

    // Read back using BufferedReader with same buffer size
    {
        uint32_t r1 = 0, r2 = 0, r3 = 0;
        CAutoFile file(fsbridge::fopen(test_file, "rb"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!file.IsNull());
        BufferedReader<CAutoFile> reader(file, sizeof(v1) + sizeof(v2));
        reader >> r1 >> r2 >> r3;
        BOOST_CHECK_EQUAL(r1, v1);
        BOOST_CHECK_EQUAL(r2, v2);
        BOOST_CHECK_EQUAL(r3, v3);
    }

    fs::remove(test_file);
}

BOOST_AUTO_TEST_CASE(span_reader_round_trip)
{
    const uint32_t v1 = 0x12345678, v2 = 0xABCDEF01;

    // Build a raw byte vector manually
    std::vector<uint8_t> data(sizeof(v1) + sizeof(v2));
    memcpy(data.data(), &v1, sizeof(v1));
    memcpy(data.data() + sizeof(v1), &v2, sizeof(v2));

    // Read back via SpanReader
    uint32_t r1 = 0, r2 = 0;
    SpanReader reader(SER_DISK, CLIENT_VERSION, data);
    reader >> r1 >> r2;
    BOOST_CHECK_EQUAL(r1, v1);
    BOOST_CHECK_EQUAL(r2, v2);

    // Reading past the end must throw
    uint32_t extra = 0;
    BOOST_CHECK_THROW(reader >> extra, std::ios_base::failure);
}

BOOST_AUTO_TEST_SUITE_END()
