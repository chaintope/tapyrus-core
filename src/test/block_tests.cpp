// Copyright (c) 2019 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>
#include <test/test_tapyrus.h>
#include <test/test_keys_helper.h>

#include <boost/test/unit_test.hpp>
#include <chainparams.h>

BOOST_FIXTURE_TEST_SUITE(block_tests, BasicTestingSetup)

CBlockHeader getBlockHeader()
{
    CBlockHeader blockHeader;
    CDataStream stream(ParseHex("010000000000000000000000000000000000000000000000000000000000000000000000f007d2a56dbebbc2a04346e624f7dff2ee0605d6ffe9622569193fddbc9280dcf007d2a56dbebbc2a04346e624f7dff2ee0605d6ffe9622569193fddbc9280dc981a335c21025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d301473045022100f434da668557be7a0c3dc366b2603c5a9706246d622050f633a082451d39249102201941554fdd618df3165269e3c855bbba8680e26defdd067ec97becfa1b296bef"), SER_NETWORK, PROTOCOL_VERSION);
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
    const std::vector<unsigned char> proof = getBlockHeader().proof;

    std::vector<unsigned char> vch;
    CVectorWriter stream(SER_NETWORK, INIT_PROTO_VERSION, vch, 0);
    stream << proof;

    std::vector<unsigned char> expected;
    expected.push_back(proof.size());
    expected.insert(expected.end(), proof.begin(), proof.end());
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
    headerWP.aggPubkey = header.aggPubkey;

    std::vector<unsigned char> vch;
    CVectorWriter stream(SER_NETWORK, INIT_PROTO_VERSION, vch, 0);
    headerWP.Serialize(stream);
    BOOST_CHECK(vch.size() == 138); // 138 bytes means size of proof excluded header
}

BOOST_AUTO_TEST_CASE(get_hash_for_sign_not_include_proof_field)
{
    CBlockHeader header = getBlockHeader();
    uint256 hash = header.GetHashForSign();
    BOOST_CHECK(hash.ToString() == "328d8580861864af5d35b263c963718ce980f057b7b481598740efd14577d4cc");
}

BOOST_AUTO_TEST_CASE(get_hash_include_proof_field)
{
    CBlockHeader header = getBlockHeader();
    uint256 hash = header.GetHash();
    BOOST_CHECK(hash.ToString() == "ee4a9fc2059c61a521958cb27c7ca9f50f7b042ea6c990d135c584b6fbbf23bc");
}

BOOST_AUTO_TEST_CASE(AbsorbBlockProof_test) {
    // Get a block
    CBlock block = getBlock();
    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << block;
    unsigned int blocksize= ssBlock.size();

    std::vector<unsigned char>  blockProof;

    createSignedBlockProof(block, blockProof);

    // serialize blockProof to get its size
    CDataStream ssBlockProof(SER_NETWORK, PROTOCOL_VERSION);
    ssBlockProof << blockProof;

    // add proof to the block
    BOOST_CHECK(block.AbsorbBlockProof(blockProof, FederationParams().GetLatestAggregatePubkey()));

    ssBlock.clear();
    ssBlock << block;

    BOOST_CHECK_EQUAL(ssBlock.size(), blocksize +  ssBlockProof.size() - 1);//-1 to account for no proof "00" in "blocksize"

    std::string blockHex = HexStr(ssBlock.begin(), ssBlock.end());
    BOOST_CHECK_EQUAL(blockHex, getSignedTestBlock());
}

BOOST_AUTO_TEST_CASE(AbsorbBlockProof_invlalid_test) {
    // Get a block
    CBlock block = getBlock();
    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << block;
    unsigned int blocksize= ssBlock.size();

    std::vector<unsigned char> blockProof;

    createSignedBlockProof(block, blockProof);

    //invalidate signature:
    // edit first <len> in [<30> <len> <02> <len R> <R> <02> <len S> <S>]
    blockProof[2] = 0x30 ;

    //returns false as all signatures in proof are not added to the block
    BOOST_CHECK_EQUAL(false, block.AbsorbBlockProof(blockProof, FederationParams().GetLatestAggregatePubkey()));

    ssBlock.clear();
    ssBlock << block;

    // only valid sig are added to the block
    BOOST_CHECK_EQUAL(ssBlock.size(), blocksize);
}

BOOST_AUTO_TEST_CASE(create_genesis_block_default)
{
    CKey aggregateKey;
    aggregateKey.Set(validAggPrivateKey, validAggPrivateKey + 32, true);
    CPubKey aggPubkey = aggregateKey.GetPubKey();

    CBlock genesis = createGenesisBlock(aggPubkey, aggregateKey);

    BOOST_CHECK_EQUAL(genesis.vtx.size(), 1);
    BOOST_CHECK_EQUAL(genesis.nVersion, 1);
    BOOST_CHECK_EQUAL(genesis.hashPrevBlock.ToString(),
    "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(genesis.hashMerkleRoot, genesis.vtx[0]->GetHash());
    BOOST_CHECK_EQUAL(genesis.hashImMerkleRoot, genesis.vtx[0]->GetHashMalFix());

    BOOST_CHECK_EQUAL(genesis.vtx[0]->vin[0].prevout.hashMalFix.ToString(), "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(genesis.vtx[0]->vin[0].prevout.n, 0);

    BOOST_CHECK_EQUAL(genesis.vtx[0]->vin.size(), 1);
    CScript scriptSig = genesis.vtx[0]->vin[0].scriptSig;
    BOOST_CHECK_EQUAL(HexStr(scriptSig.begin(), scriptSig.end()), "21025700236c2890233592fcef262f4520d22af9160e3d9705855140eb2aa06c35d3");

    BOOST_CHECK_EQUAL(genesis.vtx[0]->vout.size(), 1);
    BOOST_CHECK_EQUAL(genesis.vtx[0]->vout[0].nValue, 50 * COIN);
    CScript scriptPubKey = genesis.vtx[0]->vout[0].scriptPubKey;
    BOOST_CHECK_EQUAL(HexStr(scriptPubKey.begin(), scriptPubKey.end()), "76a914834e0737cdb9008db614cd95ec98824e952e3dc588ac");

    BOOST_CHECK_EQUAL(genesis.proof.size(),64);
}

BOOST_AUTO_TEST_SUITE_END()