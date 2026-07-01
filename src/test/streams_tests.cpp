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

        // Reading past end-of-file must throw
        DataBuffer extra(1);
        BOOST_CHECK_THROW(reader.read(extra.data(), 1), std::ios_base::failure);
    }

    fs::remove(test_file);
}

BOOST_AUTO_TEST_CASE(buffered_reader_matches_autofile_random_content)
{
    // Use a random file size and buffer size to cover all buffer boundary cases
    const size_t file_size = 1 + InsecureRandRange(1 << 17);
    const size_t buf_size  = 1 + InsecureRandRange(file_size);
    const fs::path test_file = GetDataDir() / "test_buffered_reader_random.bin";

    // Write random content directly via CAutoFile
    {
        auto random_data = insecure_rand_ctx.randbytes(file_size);
        CAutoFile file(fsbridge::fopen(test_file, "wb"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!file.IsNull());
        file.write(reinterpret_cast<const char*>(random_data.data()), file_size);
    }

    // Open the same file twice: once direct, once via BufferedReader
    {
        CAutoFile direct_file(fsbridge::fopen(test_file, "rb"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!direct_file.IsNull());

        CAutoFile buffered_file(fsbridge::fopen(test_file, "rb"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!buffered_file.IsNull());
        BufferedReader<CAutoFile> buffered_reader(buffered_file, buf_size);

        // Read in random chunk sizes and compare byte-by-byte
        for (size_t total_read = 0; total_read < file_size; ) {
            size_t max_chunk = InsecureRandBool() ? buf_size : 2 * buf_size;
            size_t read_size = 1 + InsecureRandRange(std::min(max_chunk, file_size - total_read));

            DataBuffer direct_buf(read_size), buffered_buf(read_size);
            direct_file.read(direct_buf.data(), read_size);
            buffered_reader.read(buffered_buf.data(), read_size);

            BOOST_CHECK_EQUAL_COLLECTIONS(
                direct_buf.begin(), direct_buf.end(),
                buffered_buf.begin(), buffered_buf.end());

            total_read += read_size;
        }

        // Both must throw at EOF
        DataBuffer excess(1);
        BOOST_CHECK_THROW(direct_file.read(excess.data(), 1), std::ios_base::failure);
        BOOST_CHECK_THROW(buffered_reader.read(excess.data(), 1), std::ios_base::failure);
    }

    fs::remove(test_file);
}

BOOST_AUTO_TEST_CASE(buffered_writer_matches_autofile_random_content)
{
    const size_t file_size = 1 + InsecureRandRange(1 << 17);
    const size_t buf_size  = 1 + InsecureRandRange(file_size);
    const fs::path file_direct   = GetDataDir() / "test_bw_direct.bin";
    const fs::path file_buffered = GetDataDir() / "test_bw_buffered.bin";

    // Generate random test data once
    auto test_data = insecure_rand_ctx.randbytes(file_size);

    // Write the same data to two files using direct CAutoFile vs BufferedWriter
    {
        CAutoFile direct(fsbridge::fopen(file_direct, "wb"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!direct.IsNull());

        CAutoFile buffered_file(fsbridge::fopen(file_buffered, "wb"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!buffered_file.IsNull());
        BufferedWriter<CAutoFile> buffered(buffered_file, buf_size);

        for (size_t total_written = 0; total_written < file_size; ) {
            size_t max_chunk = InsecureRandBool() ? buf_size : 2 * buf_size;
            size_t write_size = 1 + InsecureRandRange(std::min(max_chunk, file_size - total_written));

            const char* chunk = reinterpret_cast<const char*>(test_data.data()) + total_written;
            direct.write(chunk, write_size);
            buffered.write(chunk, write_size);
            total_written += write_size;
        }
        // BufferedWriter flushes in its destructor
    }

    // Read back both files and compare
    DataBuffer direct_result(file_size), buffered_result(file_size);
    {
        CAutoFile f(fsbridge::fopen(file_direct, "rb"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!f.IsNull());
        f.read(direct_result.data(), file_size);
        DataBuffer excess(1);
        BOOST_CHECK_THROW(f.read(excess.data(), 1), std::ios_base::failure);
    }
    {
        CAutoFile f(fsbridge::fopen(file_buffered, "rb"), SER_DISK, CLIENT_VERSION);
        BOOST_REQUIRE(!f.IsNull());
        f.read(buffered_result.data(), file_size);
        DataBuffer excess(1);
        BOOST_CHECK_THROW(f.read(excess.data(), 1), std::ios_base::failure);
    }

    BOOST_CHECK_EQUAL_COLLECTIONS(
        direct_result.begin(), direct_result.end(),
        buffered_result.begin(), buffered_result.end());

    fs::remove(file_direct);
    fs::remove(file_buffered);
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
