// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Each reject path of the consensus rules, driven directly: the stateless rules
// in tapyrus_consensus with HeightDerivedConsensusRules built from plain values, and the
// ConsensusInterface rules with a hand-built chain and coins view.

#include <blockindex.h>
#include <chainstatemanager.h>
#include <checkblock.h>
#include <coinsviewcache.h>
#include <consensus/merkle.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <consensusconstants.h>
#include <heightderivedconsensusrules.h>
#include <key.h>
#include <primitives/block.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <test/test_keys_helper.h>
#include <test/test_tapyrus.h>
#include <script/standard.h>
#include <tempxfieldhistory.h>
#include <xfieldhistory.h>

#include <array>
#include <optional>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(consensus_rules_tests, BasicTestingSetup)

static CKey AggregateKey()
{
    CKey key;
    key.Set(validAggPrivateKey, validAggPrivateKey + 32, true);
    return key;
}

static HeightDerivedConsensusRules ConsensusAtHeight(uint32_t maxBlockSize = ConsensusConstants::MAX_BLOCK_SIZE)
{
    return HeightDerivedConsensusRules(1, maxBlockSize, AggregateKey().GetPubKey(), SCRIPT_VERIFY_NONE);
}

static std::string Reason(const CValidationState& state) { return state.GetRejectReason(); }

static CMutableTransaction Coinbase(uint32_t height)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.n = height;
    tx.vin[0].scriptSig = CScript() << OP_0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 0;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    return tx;
}

static CMutableTransaction Spend(const COutPoint& prevout, CAmount value)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout = prevout;
    tx.vout.resize(1);
    tx.vout[0].nValue = value;
    tx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    return tx;
}

/** A block of these transactions with valid merkle roots and a proof by the test aggregate key. */
static CBlock SignedBlock(const std::vector<CMutableTransaction>& txs)
{
    CBlock block;
    block.nFeatures = ConsensusConstants::TAPYRUS_BLOCK_FEATURES;
    for (const auto& tx : txs) block.vtx.push_back(MakeTransactionRef(tx));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.hashImMerkleRoot = BlockMerkleRoot(block, nullptr, true);
    std::vector<unsigned char> proof;
    const CKey key = AggregateKey();
    BOOST_REQUIRE(key.Sign_Schnorr(block.GetHashForSign(), proof));
    BOOST_REQUIRE(block.AbsorbBlockProof(proof, key.GetPubKey()));
    return block;
}

BOOST_AUTO_TEST_CASE(check_transaction_rejects)
{
    const COutPoint prevout(uint256S("01"), 0);
    CValidationState state;
    BOOST_CHECK(CheckTransaction(CTransaction(Spend(prevout, 1)), state, ConsensusConstants::MAX_BLOCK_SIZE));

    CMutableTransaction noInputs = Spend(prevout, 1);
    noInputs.vin.clear();
    BOOST_CHECK(!CheckTransaction(CTransaction(noInputs), state, ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-vin-empty");

    CMutableTransaction noOutputs = Spend(prevout, 1);
    noOutputs.vout.clear();
    BOOST_CHECK(!CheckTransaction(CTransaction(noOutputs), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-vout-empty");

    BOOST_CHECK(!CheckTransaction(CTransaction(Spend(prevout, 1)), (state = CValidationState()), uint32_t{10}));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-oversize");

    BOOST_CHECK(!CheckTransaction(CTransaction(Spend(prevout, -1)), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-vout-negative");

    BOOST_CHECK(!CheckTransaction(CTransaction(Spend(prevout, ConsensusConstants::MAX_MONEY + 1)), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-vout-toolarge");

    CMutableTransaction overflow = Spend(prevout, ConsensusConstants::MAX_MONEY);
    overflow.vout.push_back(overflow.vout[0]);
    BOOST_CHECK(!CheckTransaction(CTransaction(overflow), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-txouttotal-toolarge");

    CMutableTransaction duplicate = Spend(prevout, 1);
    duplicate.vin.push_back(duplicate.vin[0]);
    BOOST_CHECK(!CheckTransaction(CTransaction(duplicate), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-inputs-duplicate");
    // CheckBlock skips the duplicate-input check.
    BOOST_CHECK(CheckTransaction(CTransaction(duplicate), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE, false));

    CMutableTransaction badHeight = Coinbase(0xffffffff);
    BOOST_CHECK(!CheckTransaction(CTransaction(badHeight), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-cb-length");

    CMutableTransaction longScriptSig = Coinbase(1);
    longScriptSig.vin[0].scriptSig = CScript() << std::vector<unsigned char>(100, 0);
    BOOST_CHECK(!CheckTransaction(CTransaction(longScriptSig), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-cb-length");

    CMutableTransaction nullPrevout = Spend(prevout, 1);
    nullPrevout.vin.push_back(CTxIn());
    BOOST_CHECK(!CheckTransaction(CTransaction(nullPrevout), (state = CValidationState()), ConsensusConstants::MAX_BLOCK_SIZE));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-prevout-null");
}

BOOST_AUTO_TEST_CASE(is_final_tx)
{
    CMutableTransaction tx = Spend(COutPoint(uint256S("01"), 0), 1);
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(IsFinalTx(CTransaction(tx), 100, 1000));

    // A height lock is final above that height, a time lock after that time.
    tx.nLockTime = 100;
    BOOST_CHECK(!IsFinalTx(CTransaction(tx), 100, 0));
    BOOST_CHECK(IsFinalTx(CTransaction(tx), 101, 0));
    tx.nLockTime = ConsensusConstants::LOCKTIME_THRESHOLD + 10;
    BOOST_CHECK(!IsFinalTx(CTransaction(tx), 1000000, ConsensusConstants::LOCKTIME_THRESHOLD + 10));
    BOOST_CHECK(IsFinalTx(CTransaction(tx), 0, ConsensusConstants::LOCKTIME_THRESHOLD + 11));

    // Final sequence numbers disable the lock time.
    tx.vin[0].nSequence = ConsensusConstants::SEQUENCE_FINAL;
    BOOST_CHECK(IsFinalTx(CTransaction(tx), 0, 0));
}

BOOST_AUTO_TEST_CASE(check_block_header_rejects)
{
    const CBlock block = SignedBlock({Coinbase(1)});
    CValidationState state;
    BOOST_CHECK(CheckBlockHeader(block, state, ConsensusAtHeight()));

    CBlockHeader features = block;
    features.nFeatures = ConsensusConstants::TAPYRUS_BLOCK_FEATURES + 1;
    BOOST_CHECK(!CheckBlockHeader(features, (state = CValidationState()), ConsensusAtHeight()));
    BOOST_CHECK_EQUAL(Reason(state), "bad-features");

    CBlockHeader xfield = block;
    xfield.xfield.xfieldType = TAPYRUS_XFIELDTYPES::AGGPUBKEY;
    xfield.xfield.xfieldValue = XFieldAggPubKey(std::vector<unsigned char>(33, 0));
    BOOST_CHECK(!CheckBlockHeader(xfield, (state = CValidationState()), ConsensusAtHeight()));
    BOOST_CHECK_EQUAL(Reason(state), "bad-xfieldType-xfield");

    CBlockHeader shortProof = block;
    shortProof.proof.resize(CPubKey::SCHNORR_SIGNATURE_SIZE - 1);
    BOOST_CHECK(!CheckBlockHeader(shortProof, (state = CValidationState()), ConsensusAtHeight()));
    BOOST_CHECK_EQUAL(Reason(state), "bad-proof-size");
    // Without the proof check the size is not looked at.
    BOOST_CHECK(CheckBlockHeader(shortProof, (state = CValidationState()), ConsensusAtHeight(), false));

    CKey otherKey;
    otherKey.MakeNewKey(true);
    const HeightDerivedConsensusRules otherConsensus(7, ConsensusConstants::MAX_BLOCK_SIZE, otherKey.GetPubKey(), SCRIPT_VERIFY_NONE);
    BOOST_CHECK(!CheckBlockHeader(block, (state = CValidationState()), otherConsensus));
    BOOST_CHECK_EQUAL(Reason(state), "bad-proof");
    BOOST_CHECK_EQUAL(state.GetDebugMessage(), "Proof verification failed at height [7]");
    const HeightDerivedConsensusRules unknownHeight(std::nullopt, ConsensusConstants::MAX_BLOCK_SIZE, otherKey.GetPubKey(), SCRIPT_VERIFY_NONE);
    BOOST_CHECK(!CheckBlockHeader(block, (state = CValidationState()), unknownHeight));
    BOOST_CHECK_EQUAL(state.GetDebugMessage(), "Proof verification failed at height [unknown]");
}

BOOST_AUTO_TEST_CASE(check_block_rejects)
{
    const CMutableTransaction spend = Spend(COutPoint(uint256S("01"), 0), 1);
    CValidationState state;
    BOOST_CHECK(CheckBlock(SignedBlock({Coinbase(1), spend}), state, ConsensusAtHeight()));

    CBlock merkle = SignedBlock({Coinbase(1), spend});
    merkle.hashMerkleRoot = uint256S("02");
    BOOST_CHECK(!CheckBlock(merkle, (state = CValidationState()), ConsensusAtHeight(), false));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txnmrklroot");

    CBlock immutableMerkle = SignedBlock({Coinbase(1), spend});
    immutableMerkle.hashImMerkleRoot = uint256S("02");
    BOOST_CHECK(!CheckBlock(immutableMerkle, (state = CValidationState()), ConsensusAtHeight(), false));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txnimmrklroot");

    // Repeating the last two transactions keeps the merkle root (CVE-2012-2459).
    const CMutableTransaction spend2 = Spend(COutPoint(uint256S("03"), 0), 1);
    CBlock mutated = SignedBlock({Coinbase(1), spend, spend2});
    mutated.vtx.push_back(mutated.vtx[2]);
    BOOST_CHECK(!CheckBlock(mutated, (state = CValidationState()), ConsensusAtHeight(), false));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-duplicate");

    BOOST_CHECK(!CheckBlock(SignedBlock({spend}), (state = CValidationState()), ConsensusAtHeight()));
    BOOST_CHECK_EQUAL(Reason(state), "bad-cb-missing");

    CMutableTransaction coloredCoinbase = Coinbase(1);
    coloredCoinbase.vout[0].scriptPubKey = CScript() << ColorIdentifier(COutPoint(uint256S("04"), 0), TokenTypes::REISSUABLE).toVector() << OP_COLOR << OP_TRUE;
    BOOST_CHECK(!CheckBlock(SignedBlock({coloredCoinbase}), (state = CValidationState()), ConsensusAtHeight()));
    BOOST_CHECK_EQUAL(Reason(state), "bad-cb-issuetoken");

    // The size limit is the block's own, from its rules.
    const CBlock block = SignedBlock({Coinbase(1), spend});
    const uint32_t size = ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
    BOOST_CHECK(CheckBlock(block, (state = CValidationState()), ConsensusAtHeight(size)));
    BOOST_CHECK(!CheckBlock(SignedBlock({Coinbase(1), spend}), (state = CValidationState()), ConsensusAtHeight(size - 1)));
    BOOST_CHECK_EQUAL(Reason(state), "bad-blk-length");

    BOOST_CHECK(!CheckBlock(SignedBlock({Coinbase(1), Coinbase(2)}), (state = CValidationState()), ConsensusAtHeight()));
    BOOST_CHECK_EQUAL(Reason(state), "bad-cb-multiple");

    // A failing transaction check names the transaction.
    BOOST_CHECK(!CheckBlock(SignedBlock({Coinbase(1), Spend(COutPoint(uint256S("01"), 0), -1)}), (state = CValidationState()), ConsensusAtHeight()));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-vout-negative");
    BOOST_CHECK(state.GetDebugMessage().find("Transaction check failed") == 0);

    // One sigop more than the rules allow.
    CMutableTransaction sigops = spend;
    const uint32_t limit = ConsensusAtHeight().MaxBlockSigops();
    sigops.vout[0].scriptPubKey = CScript();
    for (uint32_t i = 0; i <= limit; ++i) sigops.vout[0].scriptPubKey << OP_CHECKSIG;
    BOOST_CHECK(!CheckBlock(SignedBlock({Coinbase(1), sigops}), (state = CValidationState()), ConsensusAtHeight()));
    BOOST_CHECK_EQUAL(Reason(state), "bad-blk-sigops");
}

/** A chain of block indexes whose times rise by 10 seconds, so median times are known. */
class TestChain
{
public:
    explicit TestChain(int32_t length)
    {
        m_blocks.resize(length);
        for (int32_t i = 0; i < length; ++i) {
            m_blocks[i].nHeight = i;
            m_blocks[i].nTime = 1000 + 10 * i;
            m_blocks[i].pprev = i ? &m_blocks[i - 1] : nullptr;
        }
    }
    const CBlockIndex& Tip() const { return m_blocks.back(); }
    const CBlockIndex& At(int32_t height) const { return m_blocks.at(height); }

private:
    std::vector<CBlockIndex> m_blocks;
};

BOOST_AUTO_TEST_CASE(contextual_check_block_header_rejects)
{
    const ConsensusInterface& consensus = g_chainstate_manager.Consensus();
    const TestChain chain(20);
    const CBlockIndex& prev = chain.Tip();
    CBlockHeader header;
    header.nTime = prev.GetMedianTimePast() + 1;
    CValidationState state;
    BOOST_CHECK(consensus.ContextualCheckBlockHeader(header, state, &prev, header.nTime, nullptr));

    // A fork below the last checkpoint.
    BOOST_CHECK(consensus.ContextualCheckBlockHeader(header, state, &prev, header.nTime, &chain.At(19)));
    TestChain longer(25);
    BOOST_CHECK(!consensus.ContextualCheckBlockHeader(header, (state = CValidationState()), &prev, header.nTime, &longer.At(21)));
    BOOST_CHECK_EQUAL(Reason(state), "bad-fork-prior-to-checkpoint");

    header.nTime = prev.GetMedianTimePast();
    BOOST_CHECK(!consensus.ContextualCheckBlockHeader(header, (state = CValidationState()), &prev, header.nTime, nullptr));
    BOOST_CHECK_EQUAL(Reason(state), "time-too-old");

    header.nTime = prev.GetMedianTimePast() + 1;
    const int64_t adjustedTime = header.nTime - ConsensusConstants::MAX_FUTURE_BLOCK_TIME - 1;
    BOOST_CHECK(!consensus.ContextualCheckBlockHeader(header, (state = CValidationState()), &prev, adjustedTime, nullptr));
    BOOST_CHECK_EQUAL(Reason(state), "time-too-new");
}

BOOST_AUTO_TEST_CASE(contextual_check_block_rejects)
{
    const ConsensusInterface& consensus = g_chainstate_manager.Consensus();
    const TestChain chain(20);
    const CBlockIndex& prev = chain.Tip();
    CValidationState state;
    BOOST_CHECK(consensus.ContextualCheckBlock(SignedBlock({Coinbase(20)}), state, &prev));

    // The coinbase height must be the parent's plus one, whichever block the parent is.
    BOOST_CHECK(!consensus.ContextualCheckBlock(SignedBlock({Coinbase(21)}), (state = CValidationState()), &prev));
    BOOST_CHECK_EQUAL(Reason(state), "bad-cb-invalid");
    BOOST_CHECK(!consensus.ContextualCheckBlock(SignedBlock({Coinbase(20)}), (state = CValidationState()), &chain.At(5)));
    BOOST_CHECK_EQUAL(Reason(state), "bad-cb-invalid");
    BOOST_CHECK(consensus.ContextualCheckBlock(SignedBlock({Coinbase(6)}), (state = CValidationState()), &chain.At(5)));

    CMutableTransaction locked = Spend(COutPoint(uint256S("01"), 0), 1);
    locked.vin[0].nSequence = 0;
    locked.nLockTime = 20;
    BOOST_CHECK(!consensus.ContextualCheckBlock(SignedBlock({Coinbase(20), locked}), (state = CValidationState()), &prev));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-nonfinal");
}

BOOST_AUTO_TEST_CASE(check_tx_inputs_rejects)
{
    const ConsensusInterface& consensus = g_chainstate_manager.Consensus();
    CCoinsView base;
    CCoinsViewCache coins(&base);
    const COutPoint coinbaseOut(uint256S("01"), 0);
    const COutPoint plainOut(uint256S("02"), 0);
    const COutPoint hugeOut(uint256S("03"), 0);
    coins.AddCoin(coinbaseOut, Coin(CTxOut(10, CScript() << OP_TRUE), 10, true), false);
    coins.AddCoin(plainOut, Coin(CTxOut(10, CScript() << OP_TRUE), 10, false), false);
    coins.AddCoin(hugeOut, Coin(CTxOut(ConsensusConstants::MAX_MONEY + 1, CScript() << OP_TRUE), 10, false), false);

    CValidationState state;
    CAmount fee = 0;
    BOOST_CHECK(consensus.CheckTxInputs(CTransaction(Spend(plainOut, 7)), state, coins, 11, fee));
    BOOST_CHECK_EQUAL(fee, 3);

    BOOST_CHECK(!consensus.CheckTxInputs(CTransaction(Spend(COutPoint(uint256S("09"), 0), 1)), (state = CValidationState()), coins, 11, fee));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-inputs-missingorspent");

    // A coinbase output can be spent from the next block on.
    BOOST_CHECK(!consensus.CheckTxInputs(CTransaction(Spend(coinbaseOut, 1)), (state = CValidationState()), coins, 10, fee));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-premature-spend-of-coinbase");
    BOOST_CHECK(consensus.CheckTxInputs(CTransaction(Spend(coinbaseOut, 1)), (state = CValidationState()), coins, 11, fee));

    BOOST_CHECK(!consensus.CheckTxInputs(CTransaction(Spend(hugeOut, 1)), (state = CValidationState()), coins, 11, fee));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-inputvalues-outofrange");

    BOOST_CHECK(!consensus.CheckTxInputs(CTransaction(Spend(plainOut, 11)), (state = CValidationState()), coins, 11, fee));
    BOOST_CHECK_EQUAL(Reason(state), "bad-txns-in-belowout");
}

BOOST_AUTO_TEST_CASE(sequence_locks)
{
    const ConsensusInterface& consensus = g_chainstate_manager.Consensus();
    const TestChain chain(40);
    CBlockIndex next;
    next.pprev = const_cast<CBlockIndex*>(&chain.Tip());
    next.nHeight = chain.Tip().nHeight + 1;

    CMutableTransaction tx = Spend(COutPoint(uint256S("01"), 0), 1);
    const int flags = ConsensusConstants::LOCKTIME_VERIFY_SEQUENCE;

    // A height lock of 10 blocks on a coin confirmed at 30 is met from height 40.
    tx.vin[0].nSequence = 10;
    std::vector<int> heights{30};
    BOOST_CHECK(consensus.SequenceLocks(CTransaction(tx), flags, &heights, next));
    tx.vin[0].nSequence = 11;
    heights = {30};
    BOOST_CHECK(!consensus.SequenceLocks(CTransaction(tx), flags, &heights, next));
    // Without LOCKTIME_VERIFY_SEQUENCE, or with the disable flag, there is no lock.
    heights = {30};
    BOOST_CHECK(consensus.SequenceLocks(CTransaction(tx), 0, &heights, next));
    tx.vin[0].nSequence = 11 | ConsensusConstants::SEQUENCE_LOCKTIME_DISABLE_FLAG;
    heights = {30};
    BOOST_CHECK(consensus.SequenceLocks(CTransaction(tx), flags, &heights, next));
    BOOST_CHECK_EQUAL(heights[0], 0);

    // A time lock of one 512-second unit is not met by a chain 10 seconds apart.
    tx.vin[0].nSequence = 1 | ConsensusConstants::SEQUENCE_LOCKTIME_TYPE_FLAG;
    heights = {30};
    BOOST_CHECK(!consensus.SequenceLocks(CTransaction(tx), flags, &heights, next));
    const std::pair<int, int64_t> locks = consensus.CalculateSequenceLocks(CTransaction(tx), flags, &heights, next);
    BOOST_CHECK_EQUAL(locks.first, -1);
    BOOST_CHECK_EQUAL(locks.second, chain.At(29).GetMedianTimePast() + 512 - 1);
}

BOOST_AUTO_TEST_CASE(sigop_counts)
{
    const ConsensusInterface& consensus = g_chainstate_manager.Consensus();
    CCoinsView base;
    CCoinsViewCache coins(&base);
    const CScript redeem = CScript() << OP_CHECKSIG << OP_CHECKSIG;
    const COutPoint p2shOut(uint256S("01"), 0);
    coins.AddCoin(p2shOut, Coin(CTxOut(10, GetScriptForDestination(CScriptID(redeem))), 1, false), false);

    CMutableTransaction tx = Spend(p2shOut, 1);
    tx.vin[0].scriptSig = CScript() << ToByteVector(redeem);
    tx.vout[0].scriptPubKey = CScript() << OP_CHECKSIG;
    BOOST_CHECK_EQUAL(GetLegacySigOpCount(CTransaction(tx)), 1U);
    BOOST_CHECK_EQUAL(consensus.GetP2SHSigOpCount(CTransaction(tx), coins), 2U);
    BOOST_CHECK_EQUAL(consensus.GetTransactionSigOps(CTransaction(tx), coins, SCRIPT_VERIFY_NONE), 3U);

    // A coinbase counts only its own legacy sigops.
    CMutableTransaction coinbase = Coinbase(1);
    coinbase.vout[0].scriptPubKey = CScript() << OP_CHECKSIG;
    BOOST_CHECK_EQUAL(consensus.GetP2SHSigOpCount(CTransaction(coinbase), coins), 0U);
    BOOST_CHECK_EQUAL(consensus.GetTransactionSigOps(CTransaction(coinbase), coins, SCRIPT_VERIFY_NONE), 1U);
}

BOOST_AUTO_TEST_CASE(height_derived_rules)
{
    const ConsensusInterface& consensus = g_chainstate_manager.Consensus();
    CKey rotatedKey;
    rotatedKey.MakeNewKey(true);
    CTempXFieldHistory history(g_chainstate_manager.XFieldHistory());
    history.Add(TAPYRUS_XFIELDTYPES::MAXBLOCKSIZE, XFieldChange(XFieldMaxBlockSize(2000), 10, uint256()));
    history.Add(TAPYRUS_XFIELDTYPES::AGGPUBKEY, XFieldChange(XFieldAggPubKey(rotatedKey.GetPubKey()), 20, uint256()));

    const TestChain chain(30);
    const HeightDerivedConsensusRules before = consensus.RulesAt(chain.At(9), &history);
    BOOST_CHECK_EQUAL(*before.Height(), 9);
    BOOST_CHECK_EQUAL(before.MaxBlockSize(), ConsensusConstants::MAX_BLOCK_SIZE);
    BOOST_CHECK(before.AggregatePubKey() == AggregateKey().GetPubKey());

    // RulesAfter(prev) is RulesAt the next height.
    const HeightDerivedConsensusRules after = consensus.RulesAfter(chain.At(9), &history);
    BOOST_CHECK_EQUAL(*after.Height(), 10);
    BOOST_CHECK_EQUAL(after.MaxBlockSize(), 2000U);
    BOOST_CHECK_EQUAL(after.MaxBlockSigops(), 2000U / ConsensusConstants::BLOCK_SIZE_PER_SIGOP);

    // RulesLatest reads the history's last entries even ahead of any tip.
    const HeightDerivedConsensusRules latest = consensus.RulesLatest(&history);
    BOOST_CHECK(!latest.Height());
    BOOST_CHECK(latest.AggregatePubKey() == rotatedKey.GetPubKey());
    BOOST_CHECK_EQUAL(latest.MaxBlockSize(), 2000U);

#ifdef CP2SH_ACTIVATION_TEST_HEIGHT
    // Test builds register CP2SH at CP2SH_ACTIVATION_TEST_HEIGHT on every network.
    const TestChain activation(CP2SH_ACTIVATION_TEST_HEIGHT + 1);
    BOOST_CHECK(!(consensus.RulesAt(activation.At(0)).ScriptFlags() & SCRIPT_VERIFY_CP2SH_COLORED));
    BOOST_CHECK(!(consensus.RulesAt(activation.At(CP2SH_ACTIVATION_TEST_HEIGHT - 1)).ScriptFlags() & SCRIPT_VERIFY_CP2SH_COLORED));
    BOOST_CHECK(consensus.RulesAt(activation.At(CP2SH_ACTIVATION_TEST_HEIGHT)).ScriptFlags() & SCRIPT_VERIFY_CP2SH_COLORED);
    BOOST_CHECK_EQUAL(consensus.ActivationFlagsChange(CP2SH_ACTIVATION_TEST_HEIGHT - 1, CP2SH_ACTIVATION_TEST_HEIGHT), uint32_t{SCRIPT_VERIFY_CP2SH_COLORED});
#else
    // The test network registers no CP2SH soft fork, so it is active from genesis.
    BOOST_CHECK(consensus.RulesAt(chain.At(0)).ScriptFlags() & SCRIPT_VERIFY_CP2SH_COLORED);
    BOOST_CHECK_EQUAL(consensus.ActivationFlagsChange(0, 1), 0U);
#endif
}

BOOST_AUTO_TEST_SUITE_END()
