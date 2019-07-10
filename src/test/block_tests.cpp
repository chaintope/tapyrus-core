//
// Created by taniguchi on 2018/11/27.
//

#include <primitives/block.h>
#include <test/test_bitcoin.h>
#include <test/test_keys_helper.h>

#include <boost/test/unit_test.hpp>
#include <chainparams.h>

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

struct testKeys{
    CKey key[3];

    testKeys(){
        //private keys from test_keys_helper.h
        //corresponding public keys are:
        //03831a69b809833ab5b32612eaf489bfea35a7321b1ca15b11d88131423fafc
        //02ce7edc292d7b747fab2f23584bbafaffde5c8ff17cf689969614441e527b90
        //02785a891f323acd6ceffc59bb14304410595914267c50467e51c87142acbb5e

        const unsigned char vchKey0[32] = {0xdb,0xb9,0xd1,0x96,0x37,0x01,0x82,0x67,0x26,0x8d,0xfc,0x2c,0xc7,0xae,0xc0,0x7e,0x72,0x17,0xc1,0xa2,0xd6,0x73,0x3e,0x11,0x84,0xa0,0x90,0x92,0x73,0xbf,0x07,0x8b};
        const unsigned char vchKey1[32] = {0xae,0x6a,0xe8,0xe5,0xcc,0xbf,0xb0,0x45,0x90,0x40,0x59,0x97,0xee,0x2d,0x52,0xd2,0xb3,0x30,0x72,0x61,0x37,0xb8,0x75,0x05,0x3c,0x36,0xd9,0x4e,0x97,0x4d,0x16,0x2f};
        const unsigned char vchKey2[32] = {0x0d,0xbb,0xe8,0xe4,0xae,0x42,0x5a,0x6d,0x26,0x87,0xf1,0xa7,0xe3,0xba,0x17,0xbc,0x98,0xc6,0x73,0x63,0x67,0x90,0xf1,0xb8,0xad,0x91,0x19,0x3c,0x05,0x87,0x5e,0xf1};

        key[0].Set(vchKey0, vchKey0 + 32, true);
        key[1].Set(vchKey1, vchKey1 + 32, true);
        key[2].Set(vchKey2, vchKey2 + 32, true);
    }
};

CProof createSignedBlockProof(CBlock &block)
{
    testKeys validKeys;

    // create blockProof with 3 signatures on the block hash
    CProof blockProof;
    std::vector<unsigned char> vchSig;
    uint256 blockHash = block.GetHashForSign();

    validKeys.key[0].Sign(blockHash, vchSig);
    blockProof.addSignature(vchSig);

    validKeys.key[1].Sign(blockHash, vchSig);
    blockProof.addSignature(vchSig);

    validKeys.key[2].Sign(blockHash, vchSig);
    blockProof.addSignature(vchSig);

    return blockProof;
}

BOOST_AUTO_TEST_CASE(AbsorbBlockProof_test) {
    // Get a block
    CBlock block = getBlock();
    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << block;
    unsigned int blocksize= ssBlock.size();

    CProof blockProof = createSignedBlockProof(block);

    // serialize blockProof to get its size
    CDataStream ssBlockProof(SER_NETWORK, PROTOCOL_VERSION);
    ssBlockProof << blockProof;

    const MultisigCondition& signedBlocksCondition = Params().GetSignedBlocksCondition();
    // add proof to the block
    BOOST_CHECK(block.AbsorbBlockProof(blockProof, signedBlocksCondition));

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

    CProof blockProof = createSignedBlockProof(block);

    //invalidate signature:
    // edit first <len> in [<30> <len> <02> <len R> <R> <02> <len S> <S>]
    blockProof[2][2] = 0x30 ;

    const MultisigCondition& signedBlocksCondition = Params().GetSignedBlocksCondition();
    //returns false as all signatures in proof are not added to the block
    BOOST_CHECK_EQUAL(false, block.AbsorbBlockProof(blockProof, signedBlocksCondition));

    ssBlock.clear();
    ssBlock << block;

    // only valid sig are added to the block
    BOOST_CHECK_EQUAL(ssBlock.size(), blocksize + blockProof[0].size() + 1 + blockProof[1].size() + 1);
}

BOOST_AUTO_TEST_CASE(AbsorbBlockProof_ordering_test) {
    // Get a block
    CBlock block = getBlock();
    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << block;

    CProof blockProof = createSignedBlockProof(block);

    uint256 blockHash = block.GetHashForSign();
    const MultisigCondition& signedBlocksCondition = Params().GetSignedBlocksCondition();

    //check whether signatures are ordered according to the order of public keys
    //map: public key index = signature index
    std::map<int, int> indexMap;
    for(int i = 0, size = signedBlocksCondition.pubkeys.size(); i < size; i++ ) //public keys
    {
        for (int j = 0; j < 3; j++ ) //signatures
        {
            //verify signature
            if (signedBlocksCondition.pubkeys[i].Verify(blockHash, blockProof[j]))
            {
                indexMap[i] = j;
                break;
            }
            else
             indexMap[i] = -1;
        }
    }
    BOOST_CHECK(block.AbsorbBlockProof(blockProof, signedBlocksCondition));

    //compare order of proof inside the block with expected order in the map
    CProof::iterator blockIter = block.proof.begin();
    int count = 0;
    for(auto &mapItem : indexMap)
    {
        if(mapItem.second != -1)
        {
            count++;
            BOOST_CHECK_EQUAL(blockIter->size(), blockProof[mapItem.second].size());
            for(auto i = 0UL; i < blockIter->size(); i++)
                BOOST_CHECK_EQUAL(blockIter->data()[i], blockProof[mapItem.second][i]);
            blockIter++;
            if(blockIter == block.proof.end())
                break;
        }
    }
    BOOST_CHECK_EQUAL(count, 3);
}


BOOST_AUTO_TEST_CASE(create_genesis_block_default)
{
    MultisigCondition signedBlockCondition(combinedPubkeyString(15), 10);
    CBlock genesis = createTestGenesisBlock(signedBlockCondition, getValidPrivateKeys(15));

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
    BOOST_CHECK_EQUAL(HexStr(scriptSig.begin(), scriptSig.end()), "010a2102d7bbe714a08f73b17a3e5dcbca523470e9de5ee6c92f396beb954b8a2cdf4388");

    BOOST_CHECK_EQUAL(genesis.vtx[0]->vout.size(), 1);
    BOOST_CHECK_EQUAL(genesis.vtx[0]->vout[0].nValue, 50 * COIN);
    CScript scriptPubKey = genesis.vtx[0]->vout[0].scriptPubKey;
    BOOST_CHECK_EQUAL(HexStr(scriptPubKey.begin(), scriptPubKey.end()), "76a914c1819a5ddd545de01ed901e98a65ac905b8c389988ac");

    BOOST_CHECK_EQUAL(genesis.proof.size(), signedBlockCondition.threshold);
}

BOOST_AUTO_TEST_CASE(create_genesis_block_one_publickey)
{
    MultisigCondition condition(ValidPubKeyStrings.at(0), 1);
    auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    chainParams->SetSignedBlocksCondition(condition);
    chainParams->ReadGenesisBlock(getTestGenesisBlockHex(condition, getValidPrivateKeys(1)));

    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().vtx.size(), 1);
    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().nVersion, 1);
    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().hashPrevBlock.ToString(), "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().hashMerkleRoot, chainParams->GenesisBlock().vtx[0]->GetHash());
    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().hashImMerkleRoot, chainParams->GenesisBlock().vtx[0]->GetHashMalFix());

    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().vtx[0]->vin[0].prevout.hashMalFix.ToString(), "0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().vtx[0]->vin[0].prevout.n, 0);

    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().vtx[0]->vin.size(), 1);
    CScript scriptSig = chainParams->GenesisBlock().vtx[0]->vin[0].scriptSig;
    BOOST_CHECK_EQUAL(HexStr(scriptSig.begin(), scriptSig.end()), "01012103af80b90d25145da28c583359beb47b21796b2fe1a23c1511e443e7a64dfdb27d");

    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().vtx[0]->vout.size(), 1);
    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().vtx[0]->vout[0].nValue, 50 * COIN);
    CScript scriptPubKey = chainParams->GenesisBlock().vtx[0]->vout[0].scriptPubKey;
    BOOST_CHECK_EQUAL(HexStr(scriptPubKey.begin(), scriptPubKey.end()),
    "76a91445d405b9ed450fec89044f9b7a99a4ef6fe2cd3f88ac");

    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().proof.size(), condition.threshold);
    BOOST_CHECK_EQUAL(chainParams->GenesisBlock().GetHash(), chainParams->GetConsensus().hashGenesisBlock);

    //verify signature
    const uint256 blockHash = chainParams->GenesisBlock().GetHashForSign();
    std::vector<CPubKey>::const_iterator pubkeyIter = condition.pubkeys.begin();

    BOOST_CHECK(pubkeyIter->Verify(blockHash, chainParams->GenesisBlock().proof[0]));
}

BOOST_AUTO_TEST_SUITE_END()