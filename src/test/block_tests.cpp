//
// Created by taniguchi on 2018/11/27.
//

#include <primitives/block.h>
#include <test/test_bitcoin.h>
#include <test/test_keys_helper.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(block_tests, BasicTestingSetup)

CBlockHeader getBlockHeader()
{
    CBlockHeader blockHeader;
    CDataStream stream(ParseHex("010000000000000000000000000000000000000000000000000000000000000000000000f007d2a56dbebbc2a04346e624f7dff2ee0605d6ffe9622569193fddbc9280dcf007d2a56dbebbc2a04346e624f7dff2ee0605d6ffe9622569193fddbc9280dc981a335c01473045022100f434da668557be7a0c3dc366b2603c5a9706246d622050f633a082451d39249102201941554fdd618df3165269e3c855bbba8680e26defdd067ec97becfa1b296bef"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> blockHeader;

    assert(blockHeader.proof.size() == 1);

    return blockHeader;
}

std::string ToHex(int num)
{
    std::stringstream stream;
    stream << std::hex << num;
    return stream.str();
}

BOOST_AUTO_TEST_CASE(serialize_proof)
{
    CProof proof = getBlockHeader().proof;

    std::vector<unsigned char> vch;
    CVectorWriter stream(SER_NETWORK, INIT_PROTO_VERSION, vch, 0);
    proof.Serialize(stream);

    std::vector<unsigned char> expected;
    expected.push_back(proof.size());
    expected.push_back(proof.at(0).size());
    expected.insert(expected.end(), proof.at(0).begin(), proof.at(0).end());
    BOOST_CHECK(vch == expected);
}

BOOST_AUTO_TEST_CASE(serialized_CBlockHeader_includes_proof_data)
{
    CBlockHeader header = getBlockHeader();

    std::vector<unsigned char> vch;
    CVectorWriter stream(SER_NETWORK, INIT_PROTO_VERSION, vch, 0);
    header.Serialize(stream);
    BOOST_CHECK(vch.size() > 104); // 104 bytes means size of proof excluded header
}

BOOST_AUTO_TEST_CASE(serialized_CBlockHeaderWithoutProof_does_not_include_proof_data)
{
    CBlockHeader header = getBlockHeader();

    CBlockHeaderWithoutProof headerWP;
    headerWP.nVersion = header.nVersion;
    headerWP.hashPrevBlock = header.hashPrevBlock;
    headerWP.hashMerkleRoot = header.hashMerkleRoot;
    headerWP.hashImMerkleRoot = header.hashImMerkleRoot;
    headerWP.nTime = header.nTime;

    std::vector<unsigned char> vch;
    CVectorWriter stream(SER_NETWORK, INIT_PROTO_VERSION, vch, 0);
    headerWP.Serialize(stream);
    BOOST_CHECK(vch.size() == 104); // 104 bytes means size of proof excluded header
}

BOOST_AUTO_TEST_CASE(get_hash_for_sign_not_include_proof_field)
{
    CBlockHeader header = getBlockHeader();
    uint256 hash = header.GetHashForSign();
    BOOST_CHECK(hash.ToString() == "1c1bd2030a925b471a480507bf85c3b04cb9b5c3e5505184de3aa344dbeb9157");
}

BOOST_AUTO_TEST_CASE(get_hash_include_proof_field)
{
    CBlockHeader header = getBlockHeader();
    uint256 hash = header.GetHash();
    BOOST_CHECK(hash.ToString() == "85a678402630507e8be7adbd53bbccc7bab17b5801b317ab1a19caf24584bd3e");
}

BOOST_AUTO_TEST_SUITE_END()