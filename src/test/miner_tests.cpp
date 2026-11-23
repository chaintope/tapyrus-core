// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2019-2020 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <contextualconsensusrules.h>
#include <chainstatemanager.h>
#include <coinsviewcache.h>
#include <consensusconstants.h>
#include <consensus/merkle.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <heightderivedconsensusrules.h>
#include <validation.h>
#include <miner.h>
#include <policy/policy.h>
#include <pubkey.h>
#include <script/standard.h>
#include <txmempool.h>
#include <uint256.h>
#include <util.h>
#include <utilstrencodings.h>
#include <xfieldhistory.h>

#include <test/test_tapyrus.h>
#include <test/test_keys_helper.h>

#include <memory>
#include <stdexcept>
#include <string>

#include <boost/test/unit_test.hpp>

namespace miner_tests {
/** Mines the test's blocks from its own mempool. */
struct MinerTestingSetup : public TestingSetup {
    BlockAssembler AssemblerForTest();
    bool TestSequenceLocks(const CTransaction &tx, int flags) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void CreateBlocks(const CChainParams &chainparams,
                      std::unique_ptr<CBlockTemplate> &pblocktemplate, int &baseheight,
                      std::vector<CTransactionRef> &txFirst);
    void TestPackageSelection(const CChainParams& chainparams, const std::vector<CTransactionRef>& txFirst) EXCLUSIVE_LOCKS_REQUIRED(m_node.mempool->cs);
};
} // namespace miner_tests

BOOST_FIXTURE_TEST_SUITE(miner_tests, MinerTestingSetup)

// BOOST_CHECK_EXCEPTION predicates to check the specific validation error
class HasReason {
public:
    HasReason(const std::string& reason) : m_reason(reason) {}
    bool operator() (const std::runtime_error& e) const {
        return std::string(e.what()).find(m_reason) != std::string::npos;
    };
private:
    const std::string m_reason;
};

static CFeeRate blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);

BlockAssembler MinerTestingSetup::AssemblerForTest() {
    // The next block's full size limit, and the default -blockmintxfee (blockMinFeeRate above).
    ArgsManager args;
    args.ForceSetArg("-blockmaxsize", std::to_string(g_chainstate_manager.RulesLatest().MaxBlockSize()));
    const PolicySettings policy(args, TAPYRUS_OP_MODE::PROD);
    return BlockAssembler(g_chainstate_manager, policy, args);
}

//! Options read from the active tip, with -blockmaxsize set to max_size.
static BlockAssembler::Options OptionsWithMaxSize(const std::string& max_size)
{
    ArgsManager args;
    args.ForceSetArg("-blockmaxsize", max_size);
    const PolicySettings policy(args, TAPYRUS_OP_MODE::PROD);
    return BlockAssembler::Options(policy, args, g_chainstate_manager);
}

static
struct {
    unsigned char extranonce;
    unsigned int nonce;
} blockinfo[] = {
    {4, 0xa4a3e223}, {2, 0x15c32f9e}, {1, 0x0375b547}, {1, 0x7004a8a5},
    {2, 0xce440296}, {2, 0x52cfe198}, {1, 0x77a72cd0}, {2, 0xbb5d6f84},
    {2, 0x83f30c2c}, {1, 0x48a73d5b}, {1, 0xef7dcd01}, {2, 0x6809c6c4},
    {2, 0x0883ab3c}, {1, 0x087bbbe2}, {2, 0x2104a814}, {2, 0xdffb6daa},
    {1, 0xee8a0a08}, {2, 0xba4237c1}, {1, 0xa70349dc}, {1, 0x344722bb},
    {3, 0xd6294733}, {2, 0xec9f5c94}, {2, 0xca2fbc28}, {1, 0x6ba4f406},
    {2, 0x015d4532}, {1, 0x6e119b7c}, {2, 0x43e8f314}, {2, 0x27962f38},
    {2, 0xb571b51b}, {2, 0xb36bee23}, {2, 0xd17924a8}, {2, 0x6bc212d9},
    {1, 0x630d4948}, {2, 0x9a4c4ebb}, {2, 0x554be537}, {1, 0xd63ddfc7},
    {2, 0xa10acc11}, {1, 0x759a8363}, {2, 0xfb73090d}, {1, 0xe82c6a34},
    {1, 0xe33e92d7}, {3, 0x658ef5cb}, {2, 0xba32ff22}, {5, 0x0227a10c},
    {1, 0xa9a70155}, {5, 0xd096d809}, {1, 0x37176174}, {1, 0x830b8d0f},
    {1, 0xc6e3910e}, {2, 0x823f3ca8}, {1, 0x99850849}, {1, 0x7521fb81},
    {1, 0xaacaabab}, {1, 0xd645a2eb}, {5, 0x7aea1781}, {5, 0x9d6e4b78},
    {1, 0x4ce90fd8}, {1, 0xabdc832d}, {6, 0x4a34f32a}, {2, 0xf2524c1c},
    {2, 0x1bbeb08a}, {1, 0xad47f480}, {1, 0x9f026aeb}, {1, 0x15a95049},
    {2, 0xd1cb95b2}, {2, 0xf84bbda5}, {1, 0x0fa62cd1}, {1, 0xe05f9169},
    {1, 0x78d194a9}, {5, 0x3e38147b}, {5, 0x737ba0d4}, {1, 0x63378e10},
    {1, 0x6d5f91cf}, {2, 0x88612eb8}, {2, 0xe9639484}, {1, 0xb7fabc9d},
    {2, 0x19b01592}, {1, 0x5a90dd31}, {2, 0x5bd7e028}, {2, 0x94d00323},
    {1, 0xa9b9c01a}, {1, 0x3a40de61}, {1, 0x56e7eec7}, {5, 0x859f7ef6},
    {1, 0xfd8e5630}, {1, 0x2b0c9f7f}, {1, 0xba700e26}, {1, 0x7170a408},
    {1, 0x70de86a8}, {1, 0x74d64cd5}, {1, 0x49e738a1}, {2, 0x6910b602},
    {0, 0x643c565f}, {1, 0x54264b3f}, {2, 0x97ea6396}, {2, 0x55174459},
    {2, 0x03e8779a}, {1, 0x98f34d8f}, {1, 0xc07b2b07}, {1, 0xdfe29668},
    {1, 0x3141c7c1}, {1, 0xb3b595f4}, {1, 0x735abf08}, {5, 0x623bfbce},
    {2, 0xd351e722}, {1, 0xf4ca48c9}, {1, 0x5b19c670}, {1, 0xa164bf0e},
    {2, 0xbbbeb305}, {2, 0xfe1c810a},
};

static CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;

static CBlockIndex CreateBlockIndex(int nHeight)
{
    CBlockIndex index;
    index.nHeight = nHeight;
    index.pprev = g_chainstate_manager.ActiveChain().Tip();
    return index;
}

bool MinerTestingSetup::TestSequenceLocks(const CTransaction &tx, int flags)
{
    CTxMemPool& mempool = *m_node.mempool;
    LOCK(mempool.cs);
    CCoinsViewCache& coins_tip = g_chainstate_manager.ActiveChainstate().CoinsTip();
    CCoinsViewMemPool viewMemPool(&coins_tip, mempool);
    return CheckSequenceLocks(mempool, tx, flags, viewMemPool);
}

/**
  * Load 100 blocks and collect UTXO which is coinbase tx output for selecting txs test.
  * The UTXOs are into txFirst container.
 */
void MinerTestingSetup::CreateBlocks(const CChainParams &chainparams,
                                     std::unique_ptr<CBlockTemplate> &pblocktemplate, int &baseheight,
                                     std::vector<CTransactionRef> &txFirst)
{
    // Simple block creation, nothing special yet:
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate != nullptr);
    // We can't make transactions until we have inputs
    // Therefore, load 100 blocks :)
    for (unsigned int i = 0; i < sizeof(blockinfo)/sizeof(*blockinfo); ++i)
    {
        CBlock *pblock = &pblocktemplate->block; // pointer for convenience
        {
            LOCK(cs_main);
            pblock->nFeatures = 1;
            pblock->nTime = g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast()+1;
            CMutableTransaction txCoinbase(*pblock->vtx[0]);
            txCoinbase.nFeatures = 1;
            txCoinbase.vin[0].prevout.n = g_chainstate_manager.ActiveChain().Height() + 1;
            txCoinbase.vin[0].scriptSig = CScript();
            txCoinbase.vin[0].scriptSig.push_back(blockinfo[i].extranonce);
            txCoinbase.vin[0].scriptSig.push_back(g_chainstate_manager.ActiveChain().Height());
            txCoinbase.vout.resize(1); // Keep only the first output (the hardcoded nonces don't account for any others CreateNewBlock may add)
            txCoinbase.vout[0].scriptPubKey = CScript();
            pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
            if (txFirst.size() == 0)
                baseheight = g_chainstate_manager.ActiveChain().Height();
            if (txFirst.size() < 4)
                txFirst.push_back(pblock->vtx[0]);
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
            pblock->hashImMerkleRoot = BlockMerkleRoot(*pblock, nullptr, true);
            
            //block proof
            XFieldAggPubKey aggpubkeyChange;
            g_chainstate_manager.XFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);
            CPubKey aggpubkey(aggpubkeyChange.getPubKey());
            std::vector<unsigned char> blockProof;
            createSignedBlockProof(*pblock, blockProof);
            pblock->AbsorbBlockProof(blockProof, aggpubkey);
            BOOST_CHECK_EQUAL(pblock->proof.size(), 64);
        }
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        BOOST_CHECK(ProcessNewBlock(shared_pblock, true, nullptr));
        pblock->hashPrevBlock = pblock->GetHash();
    }

    LOCK(cs_main);
    LOCK(m_node.mempool->cs);

    // Just to make sure we can still make simple blocks
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height());
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate != nullptr);
    XFieldAggPubKey aggpubkeyChange;
    g_chainstate_manager.XFieldHistory().GetLatest(TAPYRUS_XFIELDTYPES::AGGPUBKEY, aggpubkeyChange);
    CPubKey aggpubkey(aggpubkeyChange.getPubKey());
    std::vector<unsigned char> blockProof;
    createSignedBlockProof(pblocktemplate->block, blockProof);
    pblocktemplate->block.AbsorbBlockProof(blockProof, aggpubkey);
    BOOST_CHECK_EQUAL(pblocktemplate->block.proof.size(), CPubKey::SCHNORR_SIGNATURE_SIZE);

    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1); //+1 as the new block is not added to active chain yet
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case,
// to allow reusing the blockchain created in CreateNewBlock_validity.
void MinerTestingSetup::TestPackageSelection(const CChainParams& chainparams, const std::vector<CTransactionRef>& txFirst)
{
    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent.
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hashMalFix = txFirst[0]->GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 5000000000LL - 1000;
    // This tx has a low fee: 1000 tapyrus
    uint256 hashParentTx = tx.GetHashMalFix(); // save this txid for later use
    m_node.mempool->addUnchecked(hashParentTx, entry.Fee(1000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a medium fee: 10000 tapyrus
    tx.vin[0].prevout.hashMalFix = txFirst[1]->GetHashMalFix();
    tx.vout[0].nValue = 5000000000LL - 10000;
    uint256 hashMediumFeeTx = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hashMediumFeeTx, entry.Fee(10000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

    // This tx has a high fee, but depends on the first transaction
    tx.vin[0].prevout.hashMalFix = hashParentTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000; // 50k tapyrus fee
    uint256 hashHighFeeTx = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hashHighFeeTx, entry.Fee(50000).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));

    std::unique_ptr<CBlockTemplate> pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHashMalFix() == hashParentTx);
    BOOST_CHECK(pblocktemplate->block.vtx[2]->GetHashMalFix() == hashHighFeeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[3]->GetHashMalFix() == hashMediumFeeTx);

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout.hashMalFix = hashHighFeeTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000; // 0 fee
    uint256 hashFreeTx = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hashFreeTx, entry.Fee(0).FromTx(tx));
    size_t freeTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size).
    CAmount feeToUse = blockMinFeeRate.GetFee(2*freeTxSize) - 1;

    tx.vin[0].prevout.hashMalFix = hashFreeTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000 - feeToUse;
    uint256 hashLowFeeTx = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hashLowFeeTx, entry.Fee(feeToUse).FromTx(tx));
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    // Verify that the free tx and the low fee tx didn't get selected
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHashMalFix() != hashFreeTx);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHashMalFix() != hashLowFeeTx);
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee
    // Remove the low fee transaction and replace with a higher fee transaction
    m_node.mempool->removeRecursive(tx);
    tx.vout[0].nValue -= 2; // Now we should be just over the min relay fee
    hashLowFeeTx = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hashLowFeeTx, entry.Fee(feeToUse+2).FromTx(tx));
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate->block.vtx[4]->GetHashMalFix() == hashFreeTx);
    BOOST_CHECK(pblocktemplate->block.vtx[5]->GetHashMalFix() == hashLowFeeTx);

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block.
    // Add a 0-fee transaction that has 2 outputs.
    tx.vin[0].prevout.hashMalFix = txFirst[2]->GetHashMalFix();
    tx.vout.resize(2);
    tx.vout[0].nValue = 5000000000LL - 100000000;
    tx.vout[1].nValue = 100000000; // 1TPC output
    uint256 hashFreeTx2 = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hashFreeTx2, entry.Fee(0).SpendsCoinbase(true).FromTx(tx));

    // This tx can't be mined by itself
    tx.vin[0].prevout.hashMalFix = hashFreeTx2;
    tx.vout.resize(1);
    feeToUse = blockMinFeeRate.GetFee(freeTxSize);
    tx.vout[0].nValue = 5000000000LL - 100000000 - feeToUse;
    uint256 hashLowFeeTx2 = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hashLowFeeTx2, entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);

    // Verify that this tx isn't selected.
    for (size_t i=0; i<pblocktemplate->block.vtx.size(); ++i) {
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHashMalFix() != hashFreeTx2);
        BOOST_CHECK(pblocktemplate->block.vtx[i]->GetHashMalFix() != hashLowFeeTx2);
    }

    // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
    // as well.
    tx.vin[0].prevout.n = 1;
    tx.vout[0].nValue = 100000000 - 10000; // 10k tapyrus fee
    m_node.mempool->addUnchecked(tx.GetHashMalFix(), entry.Fee(10000).FromTx(tx));
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);

    BOOST_CHECK(pblocktemplate->block.vtx.size() == 9);
    BOOST_CHECK(pblocktemplate->block.vtx[8]->GetHashMalFix() == hashLowFeeTx2);
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    // Note that by default, these tests run with size accounting enabled.
    // Uses the fixture's own genesis (TestingSetup already selected one
    // keyed with validAggPrivateKey/validAggPubKey) instead of re-deriving
    // one from the same key: createGenesisBlock()'s blockTime defaults to
    // time(0) at call time, so a second derivation isn't guaranteed to
    // hash the same as the first.
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    ChainstateOptionsOverride noCheckpoints(ChainstateOptionsOverride::TestOptions({{"-par", TestingSetup::SCRIPT_CHECK_PAR}, {"-checkpoints", "0"}}));
    CreateBlocks(Params(), pblocktemplate, baseheight, txFirst);

    LOCK(cs_main);
    LOCK(m_node.mempool->cs);

    CMutableTransaction tx;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.nHeight = 11;

    const CAmount BLOCKSUBSIDY = 50*ConsensusConstants::COIN;
    const CAmount LOWFEE = CENT;
    const CAmount HIGHFEE = ConsensusConstants::COIN;
    const CAmount HIGHERFEE = 4*ConsensusConstants::COIN;

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout.hashMalFix = txFirst[0]->GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHashMalFix();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
        m_node.mempool->addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hashMalFix = hash;
    }

    BOOST_CHECK_EXCEPTION(AssemblerForTest().CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-blk-sigops"));
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);
    m_node.mempool->clear();

    tx.vin[0].prevout.hashMalFix = txFirst[0]->GetHashMalFix();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHashMalFix();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        // If we do set the # of sig ops in the CTxMemPoolEntry, template creation passes
        m_node.mempool->addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).SigOpsCost(80).FromTx(tx));
        tx.vin[0].prevout.hashMalFix = hash;
    }
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate != nullptr);
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);
    m_node.mempool->clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 18; ++i)
        tx.vin[0].scriptSig << vchData << OP_DROP;
    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout.hashMalFix = txFirst[0]->GetHashMalFix();
    tx.vout[0].nValue = BLOCKSUBSIDY;
    for (unsigned int i = 0; i < 128; ++i)
    {
        tx.vout[0].nValue -= LOWFEE;
        hash = tx.GetHashMalFix();
        bool spendsCoinbase = i == 0; // only first tx spends coinbase
        m_node.mempool->addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hashMalFix = hash;
    }
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate != nullptr);
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);
    m_node.mempool->clear();

    // orphan in mempool, template creation fails
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest().CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    m_node.mempool->clear();

    // child with higher feerate than parent
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hashMalFix = txFirst[1]->GetHashMalFix();
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hashMalFix = hash;
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout.hashMalFix = txFirst[0]->GetHashMalFix();
    tx.vin[1].prevout.n = 0;
    tx.vout[0].nValue = tx.vout[0].nValue+BLOCKSUBSIDY-HIGHERFEE; //First txn output + fresh coinbase - new txn fee
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Fee(HIGHERFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate != nullptr);
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);
    m_node.mempool->clear();

    // coinbase in mempool, template creation fails
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vout[0].nValue = 0;
    hash = tx.GetHashMalFix();
    // give it a fee so it'll get mined
    m_node.mempool->addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    // Should throw bad-cb-multiple
    BOOST_CHECK_EXCEPTION(AssemblerForTest().CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-cb-multiple"));
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);
    m_node.mempool->clear();

    // double spend txn pair in mempool, template creation fails
    tx.vin[0].prevout.hashMalFix = txFirst[0]->GetHashMalFix();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest().CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-inputs-missingorspent"));
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);
    m_node.mempool->clear();

    // subsidy changing
    int nHeight = g_chainstate_manager.ActiveChain().Height();
    // Create an actual 209999-long block chain (without valid blocks).
    while (g_chainstate_manager.ActiveChain().Tip()->nHeight < 209999) {
        CBlockIndex* prev = g_chainstate_manager.ActiveChain().Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        g_chainstate_manager.ActiveChainstate().CoinsTip().SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        g_chainstate_manager.ActiveChain().SetTip(next);
    }
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate != nullptr);
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);

    // Extend to a 210000-long block chain.
    while (g_chainstate_manager.ActiveChain().Tip()->nHeight < 210000) {
        CBlockIndex* prev = g_chainstate_manager.ActiveChain().Tip();
        CBlockIndex* next = new CBlockIndex();
        next->phashBlock = new uint256(InsecureRand256());
        g_chainstate_manager.ActiveChainstate().CoinsTip().SetBestBlock(next->GetBlockHash());
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        g_chainstate_manager.ActiveChain().SetTip(next);
    }
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate != nullptr);
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);

    // invalid p2sh txn in mempool, template creation fails
    tx.vin[0].prevout.hashMalFix = txFirst[0]->GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(script));
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hashMalFix = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    BOOST_CHECK_EXCEPTION(AssemblerForTest().CreateNewBlock(scriptPubKey), std::runtime_error,
        HasReason("mandatory-script-verify-flag-failed (Script evaluated without error but finished with a false/empty top stack element)"));
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+1);
    m_node.mempool->clear();

    // Delete the dummy blocks again.
    while (g_chainstate_manager.ActiveChain().Tip()->nHeight > nHeight) {
        CBlockIndex* del = g_chainstate_manager.ActiveChain().Tip();
        g_chainstate_manager.ActiveChain().SetTip(del->pprev);
        g_chainstate_manager.ActiveChainstate().CoinsTip().SetBestBlock(del->pprev->GetBlockHash());
        delete del->phashBlock;
        delete del;
    }

    // non-final txs in mempool
    SetMockTime(g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast()+1);
    int flags = ConsensusConstants::LOCKTIME_VERIFY_SEQUENCE|ConsensusConstants::LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nFeatures = 1;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hashMalFix = txFirst[0]->GetHashMalFix(); // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = g_chainstate_manager.ActiveChain().Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    BOOST_CHECK(ContextualConsensusRules::SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(g_chainstate_manager.ActiveChain().Tip()->nHeight + 2))); // Sequence locks pass on 2nd block

    // relative time locked
    tx.vin[0].prevout.hashMalFix = txFirst[1]->GetHashMalFix();
    tx.vin[0].nSequence = ConsensusConstants::SEQUENCE_LOCKTIME_TYPE_FLAG | (((g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast()+1-g_chainstate_manager.ActiveChain()[1]->GetMedianTimePast()) >> ConsensusConstants::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail

    for (int i = 0; i < ConsensusConstants::nMedianTimeSpan; i++)
        g_chainstate_manager.ActiveChain().Tip()->GetAncestor(g_chainstate_manager.ActiveChain().Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    BOOST_CHECK(ContextualConsensusRules::SequenceLocks(tx, flags, &prevheights, CreateBlockIndex(g_chainstate_manager.ActiveChain().Tip()->nHeight + 1))); // Sequence locks pass 512 seconds later
    for (int i = 0; i < ConsensusConstants::nMedianTimeSpan; i++)
        g_chainstate_manager.ActiveChain().Tip()->GetAncestor(g_chainstate_manager.ActiveChain().Tip()->nHeight - i)->nTime -= 512; //undo tricked MTP

    // absolute height locked
    tx.vin[0].prevout.hashMalFix = txFirst[2]->GetHashMalFix();
    tx.vin[0].nSequence = ConsensusConstants::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = g_chainstate_manager.ActiveChain().Tip()->nHeight + 1;
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, g_chainstate_manager.ActiveChain().Tip()->nHeight + 2, g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hashMalFix = txFirst[3]->GetHashMalFix();
    tx.nLockTime = g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetHashMalFix();
    m_node.mempool->addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, g_chainstate_manager.ActiveChain().Tip()->nHeight + 2, g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast() + 1)); // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hashMalFix = hash;
    prevheights[0] = g_chainstate_manager.ActiveChain().Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    tx.vin[0].nSequence = ConsensusConstants::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = ConsensusConstants::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail

    // None of the of the absolute height/time locked tx should have made
    // it into the template because we still check IsFinalTx in CreateNewBlock,
    // but relative locked txs will if inconsistently added to mempool.
    // This template is invalid as BIP68 is always active in Tapyrus

    BOOST_CHECK_EXCEPTION(AssemblerForTest().CreateNewBlock(scriptPubKey), std::runtime_error, HasReason("bad-txns-nonfinal"));

    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 1U);
    // However if we advance height by 1 and time by 512, all of them should be mined
    for (int i = 0; i < ConsensusConstants::nMedianTimeSpan; i++)
        g_chainstate_manager.ActiveChain().Tip()->GetAncestor(g_chainstate_manager.ActiveChain().Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    g_chainstate_manager.ActiveChain().Tip()->nHeight++;
    SetMockTime(g_chainstate_manager.ActiveChain().Tip()->GetMedianTimePast() + 1);

    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey);
    BOOST_CHECK(pblocktemplate != nullptr);
    BOOST_CHECK_EQUAL(pblocktemplate->block.GetHeight(), g_chainstate_manager.ActiveChain().Height()+2); //+2 because of height increment above
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5U);

    g_chainstate_manager.ActiveChain().Tip()->nHeight--;
    SetMockTime(0);
    m_node.mempool->clear();

    TestPackageSelection(Params(), txFirst);
}

BOOST_AUTO_TEST_CASE(CreateNewBlock_required_age_in_secs)
{
    // See the comment on CreateNewBlock_validity: uses the fixture's own
    // genesis instead of re-deriving one from the same key.
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    ChainstateOptionsOverride noCheckpoints(ChainstateOptionsOverride::TestOptions({{"-par", TestingSetup::SCRIPT_CHECK_PAR}, {"-checkpoints", "0"}}));
    CreateBlocks(Params(), pblocktemplate, baseheight, txFirst);

    // Test the ancestor feerate transaction selection.
    TestMemPoolEntryHelper entry;

    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hashMalFix = txFirst[0]->GetHashMalFix();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 5000000000LL - 1000;


    uint256 hashPastTimeTx;
    {
        LOCK(m_node.mempool->cs);
        // This tx has current time. Next block is not going to include this tx.
        uint256 hashCurrentTimeTx = tx.GetHashMalFix(); // save this txid for later use
        m_node.mempool->addUnchecked(hashCurrentTimeTx,
                             entry.Fee(1000).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));

        // This tx has 60s past time. Next block is going to include this tx.
        tx.vin[0].prevout.hashMalFix = txFirst[1]->GetHashMalFix();
        hashPastTimeTx = tx.GetHashMalFix();
        m_node.mempool->addUnchecked(hashPastTimeTx,
                             entry.Fee(1000).Time(GetTime() - 60).SpendsCoinbase(true).FromTx(tx));
    }
    pblocktemplate = AssemblerForTest().CreateNewBlock(scriptPubKey, 60);
    BOOST_CHECK(pblocktemplate != nullptr);
    BOOST_CHECK(pblocktemplate->block.vtx.size() == 2); // index 0 is coinbase tx.
    BOOST_CHECK(pblocktemplate->block.vtx[1]->GetHashMalFix() == hashPastTimeTx);
}

BOOST_AUTO_TEST_CASE(the_options_read_the_next_block_from_the_tip)
{
    const BlockAssembler::Options options{OptionsWithMaxSize("50000")};
    LOCK(cs_main);
    const CBlockIndex* tip = g_chainstate_manager.ActiveChain().Tip();
    BOOST_CHECK(options.pindexPrev == tip);
    BOOST_CHECK_EQUAL(options.nHeight, tip->nHeight + 1);
    BOOST_CHECK_EQUAL(options.nLockTimeCutoff, tip->GetMedianTimePast());
    BOOST_CHECK_EQUAL(options.nBlockMaxSigops, g_chainstate_manager.RulesAfter(*tip).MaxBlockSigops());
    // Between the floor and the limit, -blockmaxsize is kept as it is.
    BOOST_CHECK_EQUAL(options.nBlockMaxSize, size_t{50000});
}

BOOST_AUTO_TEST_CASE(the_options_keep_the_block_size_within_the_limits)
{
    size_t limit;
    {
        LOCK(cs_main);
        limit = g_chainstate_manager.RulesAfter(*g_chainstate_manager.ActiveChain().Tip()).MaxBlockSize();
    }
    // Below the floor of 1000 bytes, and above the limit less 1000 bytes for the coinbase.
    BOOST_CHECK_EQUAL(OptionsWithMaxSize("500").nBlockMaxSize, size_t{1000});
    BOOST_CHECK_EQUAL(OptionsWithMaxSize(std::to_string(limit + 5000)).nBlockMaxSize, limit - 1000);
}

// Negative case: no template for a tip that is no longer active.

BOOST_AUTO_TEST_CASE(a_template_is_refused_once_the_tip_has_moved)
{
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    int baseheight = 0;
    std::vector<CTransactionRef> txFirst;
    ChainstateOptionsOverride noCheckpoints(ChainstateOptionsOverride::TestOptions({{"-par", TestingSetup::SCRIPT_CHECK_PAR}, {"-checkpoints", "0"}}));
    CreateBlocks(Params(), pblocktemplate, baseheight, txFirst);

    const BlockAssembler::Options options{OptionsWithMaxSize("50000")};
    CBlockIndex* tip;
    CValidationState state;
    {
        LOCK(cs_main);
        tip = g_chainstate_manager.ActiveChain().Tip();
        BOOST_REQUIRE(InvalidateBlock(state, tip));
    }
    BOOST_CHECK_THROW(BlockAssembler(g_chainstate_manager, options).CreateNewBlock(scriptPubKey), std::runtime_error);

    {
        LOCK(cs_main);
        ResetBlockFailureFlags(tip);
    }
    BOOST_REQUIRE(ActivateBestChain(state));
    // Back on the tip the options were read from, the template is built.
    BOOST_CHECK(BlockAssembler(g_chainstate_manager, options).CreateNewBlock(scriptPubKey) != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
