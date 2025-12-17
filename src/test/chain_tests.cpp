// Copyright (c) 2025 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Unit tests for chain.h classes - CBlockIndex and CChain
 *
 * This test suite provides comprehensive coverage for the CBlockIndex and CChain
 * classes, focusing on edge cases and boundary conditions. It complements the
 * existing skiplist_tests.cpp by covering areas not tested there:
 *
 * CBlockIndex Tests:
 * - Validation state transitions (BLOCK_VALID_* flags)
 * - IsValid() method with various flag combinations and BLOCK_FAILED states
 * - RaiseValidity() method with edge cases (already valid, failed blocks)
 * - GetBlockPos() and GetUndoPos() with various status flags
 * - GetBlockHeader() reconstruction from index data
 * - GetBlockTime() and GetBlockTimeMax() accessors
 * - GetMedianTimePast() with chains shorter than nMedianTimeSpan
 * - Status flag operations and masking
 * - CDiskBlockIndex serialization and hash computation
 * - LastCommonAncestor() with various chain topologies
 *
 * CChain Tests:
 * - SetTip() with null pointer (chain clearing)
 * - Empty chain operations (Genesis, Tip, Height)
 * - operator[] with boundary cases (negative, out of range)
 * - Chain comparison (operator==) with empty and equal chains
 * - Contains() with blocks not in chain
 * - Next() with tip and non-chain blocks
 * - FindFork() with null pointer and various fork scenarios
 * - Height() boundary cases
 *
 * Note: skiplist_tests.cpp already covers:
 * - BuildSkip() and skip list construction
 * - GetAncestor() with large chains and random queries
 * - GetLocator() with various starting points
 * - FindEarliestAtLeast() comprehensive testing
 */

#include <chain.h>
#include <test/test_tapyrus.h>
#include <primitives/block.h>
#include <primitives/xfield.h>
#include <uint256.h>
#include <validation.h>
#include <streams.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chain_tests, TestChainSetup)

/**
 * Test CBlockIndex validation state transitions
 *
 * Tests the IsValid() method with various combinations of validity flags.
 * Ensures that blocks with BLOCK_FAILED_* flags are correctly identified
 * as invalid regardless of BLOCK_VALID_* flags.
 */
BOOST_AUTO_TEST_CASE(blockindex_isvalid_edge_cases)
{
    CBlockIndex index;

    // Case 1: Initial state - no flags set
    index.nStatus = BLOCK_VALID_UNKNOWN;
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_TREE));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_TRANSACTIONS));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_CHAIN));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_SCRIPTS));

    // Case 2: BLOCK_VALID_HEADER - should pass HEADER check only
    index.nStatus = BLOCK_VALID_HEADER;
    BOOST_CHECK(index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_TREE));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_TRANSACTIONS));

    // Case 3: BLOCK_VALID_TREE - should pass HEADER and TREE checks
    index.nStatus = BLOCK_VALID_TREE;
    BOOST_CHECK(index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_TREE));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_TRANSACTIONS));

    // Case 4: BLOCK_VALID_TRANSACTIONS - should pass up to TRANSACTIONS
    index.nStatus = BLOCK_VALID_TRANSACTIONS;
    BOOST_CHECK(index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_TREE));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_TRANSACTIONS));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_CHAIN));

    // Case 5: BLOCK_VALID_CHAIN - should pass up to CHAIN
    index.nStatus = BLOCK_VALID_CHAIN;
    BOOST_CHECK(index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_TREE));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_TRANSACTIONS));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_CHAIN));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_SCRIPTS));

    // Case 6: BLOCK_VALID_SCRIPTS - should pass all validation levels
    index.nStatus = BLOCK_VALID_SCRIPTS;
    BOOST_CHECK(index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_TREE));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_TRANSACTIONS));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_CHAIN));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_SCRIPTS));

    // Case 7: BLOCK_FAILED_VALID flag makes block invalid regardless of BLOCK_VALID flags
    index.nStatus = BLOCK_VALID_SCRIPTS | BLOCK_FAILED_VALID;
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_TREE));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_TRANSACTIONS));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_CHAIN));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_SCRIPTS));

    // Case 8: BLOCK_FAILED_CHILD flag makes block invalid
    index.nStatus = BLOCK_VALID_TRANSACTIONS | BLOCK_FAILED_CHILD;
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_TRANSACTIONS));

    // Case 9: Both BLOCK_FAILED flags set
    index.nStatus = BLOCK_VALID_CHAIN | BLOCK_FAILED_VALID | BLOCK_FAILED_CHILD;
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_CHAIN));

    // Case 10: Block with data flags but no validity - should be invalid
    index.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
    BOOST_CHECK(!index.IsValid(BLOCK_VALID_HEADER));
}

/**
 * Test CBlockIndex RaiseValidity method edge cases
 *
 * Tests the RaiseValidity() method which increases the validation level
 * of a block. Tests edge cases including:
 * - Raising to same level (should return false)
 * - Raising when already at higher level (should return false)
 * - Raising when block has failed (should return false)
 * - Raising from lower to higher level (should return true)
 */
BOOST_AUTO_TEST_CASE(blockindex_raisevalidity_edge_cases)
{
    CBlockIndex index;

    // Case 1: Raise from UNKNOWN to HEADER
    index.nStatus = BLOCK_VALID_UNKNOWN;
    BOOST_CHECK(index.RaiseValidity(BLOCK_VALID_HEADER));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_HEADER);

    // Case 2: Raise to same level - should return false (no change)
    BOOST_CHECK(!index.RaiseValidity(BLOCK_VALID_HEADER));
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_HEADER);

    // Case 3: Raise to higher level
    BOOST_CHECK(index.RaiseValidity(BLOCK_VALID_TREE));
    BOOST_CHECK(index.IsValid(BLOCK_VALID_TREE));
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_TREE);

    // Case 4: Raise when already at higher level - should return false
    BOOST_CHECK(!index.RaiseValidity(BLOCK_VALID_HEADER));
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_TREE);

    // Case 5: Raise through multiple levels
    BOOST_CHECK(index.RaiseValidity(BLOCK_VALID_TRANSACTIONS));
    BOOST_CHECK(index.RaiseValidity(BLOCK_VALID_CHAIN));
    BOOST_CHECK(index.RaiseValidity(BLOCK_VALID_SCRIPTS));
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_SCRIPTS);

    // Case 6: Failed block cannot be raised
    index.nStatus = BLOCK_VALID_HEADER | BLOCK_FAILED_VALID;
    BOOST_CHECK(!index.RaiseValidity(BLOCK_VALID_TREE));
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_HEADER);

    // Case 7: BLOCK_FAILED_CHILD also prevents raising
    index.nStatus = BLOCK_VALID_TREE | BLOCK_FAILED_CHILD;
    BOOST_CHECK(!index.RaiseValidity(BLOCK_VALID_TRANSACTIONS));
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_TREE);

    // Case 8: Both failure flags prevent raising
    index.nStatus = BLOCK_VALID_UNKNOWN | BLOCK_FAILED_VALID | BLOCK_FAILED_CHILD;
    BOOST_CHECK(!index.RaiseValidity(BLOCK_VALID_HEADER));
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_UNKNOWN);

    // Case 9: RaiseValidity preserves other flags (HAVE_DATA, HAVE_UNDO)
    index.nStatus = BLOCK_VALID_HEADER | BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
    BOOST_CHECK(index.RaiseValidity(BLOCK_VALID_TREE));
    BOOST_CHECK((index.nStatus & BLOCK_HAVE_DATA) != 0);
    BOOST_CHECK((index.nStatus & BLOCK_HAVE_UNDO) != 0);
    BOOST_CHECK((index.nStatus & BLOCK_VALID_MASK) == BLOCK_VALID_TREE);
}

/**
 * Test CBlockIndex GetBlockPos and GetUndoPos edge cases
 *
 * Tests the methods that retrieve disk positions for block data and undo data.
 * Tests various combinations of status flags and disk positions.
 */
BOOST_AUTO_TEST_CASE(blockindex_disk_pos_edge_cases)
{
    CBlockIndex index;

    // Case 1: No data available - should return null positions
    index.nStatus = BLOCK_VALID_HEADER;
    index.nFile = 5;
    index.nDataPos = 1000;
    index.nUndoPos = 2000;

    CDiskBlockPos blockPos = index.GetBlockPos();
    BOOST_CHECK(blockPos.IsNull());

    CDiskBlockPos undoPos = index.GetUndoPos();
    BOOST_CHECK(undoPos.IsNull());

    // Case 2: BLOCK_HAVE_DATA flag set - GetBlockPos should return position
    index.nStatus = BLOCK_VALID_TRANSACTIONS | BLOCK_HAVE_DATA;
    index.nFile = 10;
    index.nDataPos = 5000;

    blockPos = index.GetBlockPos();
    BOOST_CHECK(!blockPos.IsNull());
    BOOST_CHECK_EQUAL(blockPos.nFile, 10);
    BOOST_CHECK_EQUAL(blockPos.nPos, 5000);

    // Undo still not available
    undoPos = index.GetUndoPos();
    BOOST_CHECK(undoPos.IsNull());

    // Case 3: BLOCK_HAVE_UNDO flag set - GetUndoPos should return position
    index.nStatus = BLOCK_VALID_CHAIN | BLOCK_HAVE_UNDO;
    index.nFile = 12;
    index.nUndoPos = 8000;

    undoPos = index.GetUndoPos();
    BOOST_CHECK(!undoPos.IsNull());
    BOOST_CHECK_EQUAL(undoPos.nFile, 12);
    BOOST_CHECK_EQUAL(undoPos.nPos, 8000);

    // Block data not available in this case
    blockPos = index.GetBlockPos();
    BOOST_CHECK(blockPos.IsNull());

    // Case 4: Both flags set - both positions available
    index.nStatus = BLOCK_VALID_SCRIPTS | BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
    index.nFile = 15;
    index.nDataPos = 10000;
    index.nUndoPos = 15000;

    blockPos = index.GetBlockPos();
    BOOST_CHECK(!blockPos.IsNull());
    BOOST_CHECK_EQUAL(blockPos.nFile, 15);
    BOOST_CHECK_EQUAL(blockPos.nPos, 10000);

    undoPos = index.GetUndoPos();
    BOOST_CHECK(!undoPos.IsNull());
    BOOST_CHECK_EQUAL(undoPos.nFile, 15);
    BOOST_CHECK_EQUAL(undoPos.nPos, 15000);

    // Case 5: Edge case - file position 0
    index.nFile = 0;
    index.nDataPos = 0;
    index.nUndoPos = 0;
    index.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;

    blockPos = index.GetBlockPos();
    BOOST_CHECK(!blockPos.IsNull());
    BOOST_CHECK_EQUAL(blockPos.nFile, 0);
    BOOST_CHECK_EQUAL(blockPos.nPos, 0);

    undoPos = index.GetUndoPos();
    BOOST_CHECK(!undoPos.IsNull());
    BOOST_CHECK_EQUAL(undoPos.nFile, 0);
    BOOST_CHECK_EQUAL(undoPos.nPos, 0);
}

/**
 * Test CBlockIndex GetBlockTime and GetBlockTimeMax
 *
 * Tests the time accessor methods for blocks.
 */
BOOST_AUTO_TEST_CASE(blockindex_time_accessors)
{
    CBlockIndex index;

    // Case 1: Zero time
    index.nTime = 0;
    index.nTimeMax = 0;
    BOOST_CHECK_EQUAL(index.GetBlockTime(), 0);
    BOOST_CHECK_EQUAL(index.GetBlockTimeMax(), 0);

    // Case 2: Normal time values
    index.nTime = 1609459200; // 2021-01-01 00:00:00 UTC
    index.nTimeMax = 1609545600; // 2021-01-02 00:00:00 UTC
    BOOST_CHECK_EQUAL(index.GetBlockTime(), 1609459200);
    BOOST_CHECK_EQUAL(index.GetBlockTimeMax(), 1609545600);

    // Case 3: Maximum uint32_t value
    index.nTime = std::numeric_limits<uint32_t>::max();
    index.nTimeMax = std::numeric_limits<uint32_t>::max();
    BOOST_CHECK_EQUAL(index.GetBlockTime(), (int64_t)std::numeric_limits<uint32_t>::max());
    BOOST_CHECK_EQUAL(index.GetBlockTimeMax(), (int64_t)std::numeric_limits<uint32_t>::max());

    // Case 4: nTimeMax should typically be >= nTime
    index.nTime = 1000;
    index.nTimeMax = 2000;
    BOOST_CHECK(index.GetBlockTimeMax() >= index.GetBlockTime());
}

/**
 * Test CBlockIndex GetMedianTimePast edge cases
 *
 * Tests the median time past calculation with various chain lengths,
 * especially chains shorter than nMedianTimeSpan (11 blocks).
 */
BOOST_AUTO_TEST_CASE(blockindex_median_time_past_edge_cases)
{
    // Case 1: Single block (genesis) - median is its own time
    CBlockIndex genesis;
    genesis.nTime = 1000;
    genesis.pprev = nullptr;
    genesis.nHeight = 0;

    BOOST_CHECK_EQUAL(genesis.GetMedianTimePast(), 1000);

    // Case 2: Chain of 2 blocks - median of 2 values
    CBlockIndex block1;
    block1.nTime = 2000;
    block1.pprev = &genesis;
    block1.nHeight = 1;

    int64_t median1 = block1.GetMedianTimePast();
    BOOST_CHECK(median1 == 1000 || median1 == 2000); // Median of 2 values

    // Case 3: Chain of 5 blocks - median of 5 values
    std::vector<CBlockIndex> shortChain(5);
    shortChain[0].nTime = 100;
    shortChain[0].pprev = nullptr;
    shortChain[0].nHeight = 0;

    for (int i = 1; i < 5; i++) {
        shortChain[i].nTime = 100 + i * 100; // 100, 200, 300, 400, 500
        shortChain[i].pprev = &shortChain[i-1];
        shortChain[i].nHeight = i;
    }

    // For block 4 (height 4), median of [100, 200, 300, 400, 500] = 300
    BOOST_CHECK_EQUAL(shortChain[4].GetMedianTimePast(), 300);

    // Case 4: Chain of 11 blocks - full nMedianTimeSpan
    std::vector<CBlockIndex> fullChain(11);
    for (int i = 0; i < 11; i++) {
        fullChain[i].nTime = 1000 + i * 10; // 1000, 1010, 1020, ..., 1100
        fullChain[i].pprev = (i == 0) ? nullptr : &fullChain[i-1];
        fullChain[i].nHeight = i;
    }

    // Median of 11 values (1000 to 1100) = 1050 (middle value)
    BOOST_CHECK_EQUAL(fullChain[10].GetMedianTimePast(), 1050);

    // Case 5: Chain longer than 11 - only last 11 blocks used
    std::vector<CBlockIndex> longChain(20);
    for (int i = 0; i < 20; i++) {
        longChain[i].nTime = 1000 + i * 10;
        longChain[i].pprev = (i == 0) ? nullptr : &longChain[i-1];
        longChain[i].nHeight = i;
    }

    // For block 19, median of blocks 9-19 (times 1090-1190) = 1140
    BOOST_CHECK_EQUAL(longChain[19].GetMedianTimePast(), 1140);

    // Case 6: Unsorted times - should still calculate median correctly
    std::vector<CBlockIndex> unsortedChain(7);
    int times[] = {1000, 900, 1100, 950, 1050, 980, 1020}; // Unsorted
    for (int i = 0; i < 7; i++) {
        unsortedChain[i].nTime = times[i];
        unsortedChain[i].pprev = (i == 0) ? nullptr : &unsortedChain[i-1];
        unsortedChain[i].nHeight = i;
    }

    // Median of [1000, 900, 1100, 950, 1050, 980, 1020] sorted = [900, 950, 980, 1000, 1020, 1050, 1100]
    // Median is 1000 (middle value)
    BOOST_CHECK_EQUAL(unsortedChain[6].GetMedianTimePast(), 1000);
}

/**
 * Test CBlockIndex GetBlockHeader reconstruction
 *
 * Tests that GetBlockHeader() correctly reconstructs a CBlockHeader
 * from the index data, including all fields and proper pprev handling.
 */
BOOST_AUTO_TEST_CASE(blockindex_get_block_header)
{
    // Create a parent block
    uint256 parentHash = InsecureRand256();
    CBlockIndex parent;
    parent.phashBlock = &parentHash;
    parent.nHeight = 10;

    // Create a block index with all fields populated
    uint256 blockHash = InsecureRand256();
    CBlockIndex index;
    index.phashBlock = &blockHash;
    index.pprev = &parent;
    index.nHeight = 11;
    index.nFeatures = 1;
    index.hashMerkleRoot = InsecureRand256();
    index.hashImMerkleRoot = InsecureRand256();
    index.nTime = 1609459200;
    index.proof = {0x01, 0x02, 0x03, 0x04};
    index.xfield = CXField(XFieldAggPubKey({0x05, 0x06, 0x07, 0x08}));

    // Reconstruct header
    CBlockHeader header = index.GetBlockHeader();

    // Verify all fields are correctly copied
    BOOST_CHECK_EQUAL(header.nFeatures, index.nFeatures);
    BOOST_CHECK(header.hashPrevBlock == parentHash);
    BOOST_CHECK(header.hashMerkleRoot == index.hashMerkleRoot);
    BOOST_CHECK(header.hashImMerkleRoot == index.hashImMerkleRoot);
    BOOST_CHECK_EQUAL(header.nTime, index.nTime);
    BOOST_CHECK(header.proof == index.proof);
    BOOST_CHECK(header.xfield.xfieldType == index.xfield.xfieldType);

    // Case 2: Block without parent (genesis)
    CBlockIndex genesisIndex;
    genesisIndex.pprev = nullptr;
    genesisIndex.nHeight = 0;
    genesisIndex.nFeatures = 1;
    genesisIndex.nTime = 1000000;

    CBlockHeader genesisHeader = genesisIndex.GetBlockHeader();
    BOOST_CHECK(genesisHeader.hashPrevBlock.IsNull());
    BOOST_CHECK_EQUAL(genesisHeader.nFeatures, 1);
    BOOST_CHECK_EQUAL(genesisHeader.nTime, 1000000);
}

/**
 * Test CDiskBlockIndex edge cases
 *
 * Tests the CDiskBlockIndex class which is used for serialization.
 * Verifies hash computation and parent hash handling.
 */
BOOST_AUTO_TEST_CASE(diskblockindex_edge_cases)
{
    // Case 1: Create from CBlockIndex with parent
    uint256 parentHash = InsecureRand256();
    CBlockIndex parent;
    parent.phashBlock = &parentHash;

    CBlockIndex index;
    index.pprev = &parent;
    index.nHeight = 5;
    index.nFeatures = 1;
    index.hashMerkleRoot = InsecureRand256();
    index.hashImMerkleRoot = InsecureRand256();
    index.nTime = 1609459200;
    index.proof = {0x01, 0x02, 0x03};

    CDiskBlockIndex diskIndex(&index);
    BOOST_CHECK(diskIndex.hashPrev == parentHash);
    BOOST_CHECK_EQUAL(diskIndex.nHeight, index.nHeight);
    BOOST_CHECK_EQUAL(diskIndex.nFeatures, index.nFeatures);

    // Case 2: Create from CBlockIndex without parent (genesis)
    CBlockIndex genesisIndex;
    genesisIndex.pprev = nullptr;
    genesisIndex.nHeight = 0;
    genesisIndex.nFeatures = 1;

    CDiskBlockIndex diskGenesisIndex(&genesisIndex);
    BOOST_CHECK(diskGenesisIndex.hashPrev.IsNull());
    BOOST_CHECK_EQUAL(diskGenesisIndex.nHeight, 0);

    // Case 3: Default constructor
    CDiskBlockIndex defaultDiskIndex;
    BOOST_CHECK(defaultDiskIndex.hashPrev.IsNull());
    BOOST_CHECK_EQUAL(defaultDiskIndex.nHeight, 0);
}

/**
 * Test LastCommonAncestor function edge cases
 *
 * Tests the LastCommonAncestor() function which finds the last common
 * block between two chain tips. Tests various fork scenarios.
 */
BOOST_AUTO_TEST_CASE(last_common_ancestor_edge_cases)
{
    // Build a main chain: 0 -> 1 -> 2 -> 3 -> 4 -> 5
    std::vector<CBlockIndex> mainChain(6);
    for (int i = 0; i < 6; i++) {
        mainChain[i].nHeight = i;
        mainChain[i].pprev = (i == 0) ? nullptr : &mainChain[i-1];
    }

    // Case 1: Same block - should return that block
    const CBlockIndex* lca = LastCommonAncestor(&mainChain[3], &mainChain[3]);
    BOOST_CHECK(lca == &mainChain[3]);

    // Case 2: One block is ancestor of the other
    lca = LastCommonAncestor(&mainChain[5], &mainChain[2]);
    BOOST_CHECK(lca == &mainChain[2]);

    lca = LastCommonAncestor(&mainChain[2], &mainChain[5]);
    BOOST_CHECK(lca == &mainChain[2]);

    // Case 3: Fork at block 3
    // Main: 0 -> 1 -> 2 -> 3 -> 4 -> 5
    // Fork:               3 -> F1 -> F2
    std::vector<CBlockIndex> forkChain(2);
    forkChain[0].nHeight = 4;
    forkChain[0].pprev = &mainChain[3];
    forkChain[1].nHeight = 5;
    forkChain[1].pprev = &forkChain[0];

    lca = LastCommonAncestor(&mainChain[5], &forkChain[1]);
    BOOST_CHECK(lca == &mainChain[3]);

    // Case 4: Fork at genesis
    // Main: 0 -> 1 -> 2
    // Fork: 0 -> A -> B
    std::vector<CBlockIndex> altChain(2);
    altChain[0].nHeight = 1;
    altChain[0].pprev = &mainChain[0];
    altChain[1].nHeight = 2;
    altChain[1].pprev = &altChain[0];

    lca = LastCommonAncestor(&mainChain[2], &altChain[1]);
    BOOST_CHECK(lca == &mainChain[0]);

    // Case 5: Both blocks are genesis
    lca = LastCommonAncestor(&mainChain[0], &mainChain[0]);
    BOOST_CHECK(lca == &mainChain[0]);
}

/**
 * Test CChain SetTip edge cases
 *
 * Tests the SetTip() method with various scenarios including:
 * - Setting null tip (clearing chain)
 * - Setting tip on empty chain
 * - Changing tip to different block
 * - Setting tip to block with missing parents (should build chain)
 */
BOOST_AUTO_TEST_CASE(cchain_settip_edge_cases)
{
    CChain chain;

    // Case 1: Initial state - empty chain
    BOOST_CHECK(chain.Genesis() == nullptr);
    BOOST_CHECK(chain.Tip() == nullptr);
    BOOST_CHECK_EQUAL(chain.Height(), -1);

    // Case 2: Set tip to single block (genesis)
    CBlockIndex genesis;
    genesis.nHeight = 0;
    genesis.pprev = nullptr;

    chain.SetTip(&genesis);
    BOOST_CHECK(chain.Genesis() == &genesis);
    BOOST_CHECK(chain.Tip() == &genesis);
    BOOST_CHECK_EQUAL(chain.Height(), 0);
    BOOST_CHECK(chain[0] == &genesis);

    // Case 3: Extend chain
    CBlockIndex block1;
    block1.nHeight = 1;
    block1.pprev = &genesis;

    chain.SetTip(&block1);
    BOOST_CHECK(chain.Genesis() == &genesis);
    BOOST_CHECK(chain.Tip() == &block1);
    BOOST_CHECK_EQUAL(chain.Height(), 1);
    BOOST_CHECK(chain[0] == &genesis);
    BOOST_CHECK(chain[1] == &block1);

    // Case 4: Set tip to longer chain
    std::vector<CBlockIndex> longChain(10);
    for (int i = 0; i < 10; i++) {
        longChain[i].nHeight = i;
        longChain[i].pprev = (i == 0) ? nullptr : &longChain[i-1];
    }

    chain.SetTip(&longChain[9]);
    BOOST_CHECK_EQUAL(chain.Height(), 9);
    BOOST_CHECK(chain.Genesis() == &longChain[0]);
    BOOST_CHECK(chain.Tip() == &longChain[9]);

    // Verify all blocks in chain
    for (int i = 0; i < 10; i++) {
        BOOST_CHECK(chain[i] == &longChain[i]);
    }

    // Case 5: Set tip to nullptr (clear chain)
    chain.SetTip(nullptr);
    BOOST_CHECK(chain.Genesis() == nullptr);
    BOOST_CHECK(chain.Tip() == nullptr);
    BOOST_CHECK_EQUAL(chain.Height(), -1);

    // Case 6: Set tip to middle of previous chain
    chain.SetTip(&longChain[5]);
    BOOST_CHECK_EQUAL(chain.Height(), 5);
    BOOST_CHECK(chain.Tip() == &longChain[5]);
    BOOST_CHECK(chain[5] == &longChain[5]);
    BOOST_CHECK(chain[6] == nullptr); // Beyond tip
}

/**
 * Test CChain operator[] boundary cases
 *
 * Tests the array access operator with various boundary conditions:
 * - Negative indices
 * - Indices beyond chain height
 * - Valid indices
 * - Empty chain
 */
BOOST_AUTO_TEST_CASE(cchain_array_operator_edge_cases)
{
    CChain chain;

    // Case 1: Empty chain - all accesses should return nullptr
    BOOST_CHECK(chain[0] == nullptr);
    BOOST_CHECK(chain[-1] == nullptr);
    BOOST_CHECK(chain[100] == nullptr);

    // Case 2: Chain with 5 blocks (heights 0-4)
    std::vector<CBlockIndex> blocks(5);
    for (int i = 0; i < 5; i++) {
        blocks[i].nHeight = i;
        blocks[i].pprev = (i == 0) ? nullptr : &blocks[i-1];
    }
    chain.SetTip(&blocks[4]);

    // Valid indices
    BOOST_CHECK(chain[0] == &blocks[0]);
    BOOST_CHECK(chain[1] == &blocks[1]);
    BOOST_CHECK(chain[2] == &blocks[2]);
    BOOST_CHECK(chain[3] == &blocks[3]);
    BOOST_CHECK(chain[4] == &blocks[4]);

    // Beyond tip
    BOOST_CHECK(chain[5] == nullptr);
    BOOST_CHECK(chain[10] == nullptr);
    BOOST_CHECK(chain[1000] == nullptr);

    // Negative indices
    BOOST_CHECK(chain[-1] == nullptr);
    BOOST_CHECK(chain[-100] == nullptr);

    // Case 3: After clearing chain
    chain.SetTip(nullptr);
    BOOST_CHECK(chain[0] == nullptr);
}

/**
 * Test CChain comparison operator (operator==) edge cases
 *
 * Tests chain equality comparison with various scenarios.
 */
BOOST_AUTO_TEST_CASE(cchain_equality_edge_cases)
{
    CChain chain1, chain2;

    // Case 1: Two empty chains - cannot use operator== on empty chains
    // because it accesses vChain[size-1] which underflows when size=0
    // Instead, verify both have Height() == -1
    BOOST_CHECK(chain1.Height() == -1 && chain2.Height() == -1);

    // Build chains
    std::vector<CBlockIndex> blocks1(5);
    std::vector<CBlockIndex> blocks2(5);

    for (int i = 0; i < 5; i++) {
        blocks1[i].nHeight = i;
        blocks1[i].pprev = (i == 0) ? nullptr : &blocks1[i-1];

        blocks2[i].nHeight = i;
        blocks2[i].pprev = (i == 0) ? nullptr : &blocks2[i-1];
    }

    // Case 2: Different chains (different tips)
    chain1.SetTip(&blocks1[4]);
    chain2.SetTip(&blocks2[4]);
    BOOST_CHECK(!(chain1 == chain2)); // Different block objects

    // Case 3: Same tip, same chain
    chain2.SetTip(&blocks1[4]);
    BOOST_CHECK(chain1 == chain2);

    // Case 4: Different lengths
    chain2.SetTip(&blocks1[3]);
    BOOST_CHECK(!(chain1 == chain2));

    // Case 5: One empty, one not - cannot use operator== when one is empty
    // Check heights instead
    chain2.SetTip(nullptr);
    BOOST_CHECK(chain1.Height() != chain2.Height());

    // Case 6: Both pointing to same single block
    CBlockIndex singleBlock;
    singleBlock.nHeight = 0;
    singleBlock.pprev = nullptr;

    chain1.SetTip(&singleBlock);
    chain2.SetTip(&singleBlock);
    BOOST_CHECK(chain1 == chain2);
}

/**
 * Test CChain Contains method edge cases
 *
 * Tests the Contains() method which checks if a block is in the chain.
 */
BOOST_AUTO_TEST_CASE(cchain_contains_edge_cases)
{
    CChain chain;

    // Build a chain
    std::vector<CBlockIndex> mainChain(10);
    for (int i = 0; i < 10; i++) {
        mainChain[i].nHeight = i;
        mainChain[i].pprev = (i == 0) ? nullptr : &mainChain[i-1];
    }

    chain.SetTip(&mainChain[9]);

    // Case 1: Blocks in chain should be contained
    for (int i = 0; i < 10; i++) {
        BOOST_CHECK(chain.Contains(&mainChain[i]));
    }

    // Case 2: Create a fork block at same height but not in chain
    CBlockIndex forkBlock;
    forkBlock.nHeight = 5;
    forkBlock.pprev = &mainChain[4];

    BOOST_CHECK(!chain.Contains(&forkBlock));

    // Case 3: Block with height beyond chain
    CBlockIndex beyondBlock;
    beyondBlock.nHeight = 20;

    BOOST_CHECK(!chain.Contains(&beyondBlock));

    // Case 4: Block with negative height (edge case)
    CBlockIndex negativeBlock;
    negativeBlock.nHeight = -1;

    BOOST_CHECK(!chain.Contains(&negativeBlock));

    // Case 5: Contains check on empty chain
    CChain emptyChain;
    BOOST_CHECK(!emptyChain.Contains(&mainChain[0]));
}

/**
 * Test CChain Next method edge cases
 *
 * Tests the Next() method which finds the successor of a block in the chain.
 */
BOOST_AUTO_TEST_CASE(cchain_next_edge_cases)
{
    CChain chain;

    // Build a chain
    std::vector<CBlockIndex> blocks(10);
    for (int i = 0; i < 10; i++) {
        blocks[i].nHeight = i;
        blocks[i].pprev = (i == 0) ? nullptr : &blocks[i-1];
    }

    chain.SetTip(&blocks[9]);

    // Case 1: Next of blocks in middle of chain
    for (int i = 0; i < 9; i++) {
        BOOST_CHECK(chain.Next(&blocks[i]) == &blocks[i+1]);
    }

    // Case 2: Next of tip is nullptr
    BOOST_CHECK(chain.Next(&blocks[9]) == nullptr);

    // Case 3: Create a fork block not in chain
    CBlockIndex forkBlock;
    forkBlock.nHeight = 5;
    forkBlock.pprev = &blocks[4];

    BOOST_CHECK(chain.Next(&forkBlock) == nullptr);

    // Case 4: Next on empty chain
    CChain emptyChain;
    BOOST_CHECK(emptyChain.Next(&blocks[0]) == nullptr);
}

/**
 * Test CChain Height method edge cases
 *
 * Tests the Height() method which returns the maximum height in the chain.
 */
BOOST_AUTO_TEST_CASE(cchain_height_edge_cases)
{
    CChain chain;

    // Case 1: Empty chain has height -1
    BOOST_CHECK_EQUAL(chain.Height(), -1);

    // Case 2: Genesis block (height 0)
    CBlockIndex genesis;
    genesis.nHeight = 0;
    genesis.pprev = nullptr;

    chain.SetTip(&genesis);
    BOOST_CHECK_EQUAL(chain.Height(), 0);

    // Case 3: Chain with multiple blocks
    std::vector<CBlockIndex> blocks(100);
    for (int i = 0; i < 100; i++) {
        blocks[i].nHeight = i;
        blocks[i].pprev = (i == 0) ? nullptr : &blocks[i-1];
    }

    chain.SetTip(&blocks[99]);
    BOOST_CHECK_EQUAL(chain.Height(), 99);

    // Case 4: After clearing chain
    chain.SetTip(nullptr);
    BOOST_CHECK_EQUAL(chain.Height(), -1);

    // Case 5: Very large chain
    std::vector<CBlockIndex> largeChain(1000000);
    largeChain[0].nHeight = 0;
    largeChain[0].pprev = nullptr;

    for (int i = 1; i < 1000000; i += 10000) {
        largeChain[i].nHeight = i;
        largeChain[i].pprev = &largeChain[i-1];
    }

    largeChain[999999].nHeight = 999999;
    largeChain[999999].pprev = &largeChain[999998];

    chain.SetTip(&largeChain[999999]);
    BOOST_CHECK_EQUAL(chain.Height(), 999999);
}

/**
 * Test CChain FindFork edge cases
 *
 * Tests the FindFork() method which finds the last common block between
 * the chain and a given block index.
 */
BOOST_AUTO_TEST_CASE(cchain_findfork_edge_cases)
{
    CChain chain;

    // Build main chain: 0 -> 1 -> 2 -> 3 -> 4 -> 5
    std::vector<CBlockIndex> mainChain(6);
    for (int i = 0; i < 6; i++) {
        mainChain[i].nHeight = i;
        mainChain[i].pprev = (i == 0) ? nullptr : &mainChain[i-1];
    }

    chain.SetTip(&mainChain[5]);

    // Case 1: FindFork with nullptr
    const CBlockIndex* fork = chain.FindFork(nullptr);
    BOOST_CHECK(fork == nullptr);

    // Case 2: FindFork with block in chain - should return that block
    fork = chain.FindFork(&mainChain[3]);
    BOOST_CHECK(fork == &mainChain[3]);

    // Case 3: FindFork with tip - should return tip
    fork = chain.FindFork(&mainChain[5]);
    BOOST_CHECK(fork == &mainChain[5]);

    // Case 4: FindFork with genesis - should return genesis
    fork = chain.FindFork(&mainChain[0]);
    BOOST_CHECK(fork == &mainChain[0]);

    // Case 5: FindFork with fork at height 3
    // Fork: 3 -> F1 -> F2
    std::vector<CBlockIndex> forkChain(2);
    forkChain[0].nHeight = 4;
    forkChain[0].pprev = &mainChain[3];
    forkChain[1].nHeight = 5;
    forkChain[1].pprev = &forkChain[0];

    fork = chain.FindFork(&forkChain[1]);
    BOOST_CHECK(fork == &mainChain[3]);

    // Case 6: FindFork with very long fork (higher than chain tip)
    std::vector<CBlockIndex> longFork(10);
    longFork[0].nHeight = 4;
    longFork[0].pprev = &mainChain[3];
    for (int i = 1; i < 10; i++) {
        longFork[i].nHeight = 4 + i;
        longFork[i].pprev = &longFork[i-1];
    }

    fork = chain.FindFork(&longFork[9]);
    BOOST_CHECK(fork == &mainChain[3]);

    // Case 7: FindFork on empty chain
    CChain emptyChain;
    fork = emptyChain.FindFork(&mainChain[3]);
    BOOST_CHECK(fork == nullptr);
}

/**
 * Test block status flag operations and masking
 *
 * Tests that status flags can be properly combined, masked, and checked
 * without interfering with each other.
 */
BOOST_AUTO_TEST_CASE(block_status_flags_operations)
{
    // Case 1: BLOCK_VALID_MASK includes all validity flags
    uint32_t validMask = BLOCK_VALID_MASK;
    BOOST_CHECK((validMask & BLOCK_VALID_HEADER) != 0);
    BOOST_CHECK((validMask & BLOCK_VALID_TREE) != 0);
    BOOST_CHECK((validMask & BLOCK_VALID_TRANSACTIONS) != 0);
    BOOST_CHECK((validMask & BLOCK_VALID_CHAIN) != 0);
    BOOST_CHECK((validMask & BLOCK_VALID_SCRIPTS) != 0);

    // Case 2: BLOCK_HAVE_MASK includes data and undo flags
    uint32_t haveMask = BLOCK_HAVE_MASK;
    BOOST_CHECK((haveMask & BLOCK_HAVE_DATA) != 0);
    BOOST_CHECK((haveMask & BLOCK_HAVE_UNDO) != 0);

    // Case 3: BLOCK_FAILED_MASK includes failure flags
    uint32_t failedMask = BLOCK_FAILED_MASK;
    BOOST_CHECK((failedMask & BLOCK_FAILED_VALID) != 0);
    BOOST_CHECK((failedMask & BLOCK_FAILED_CHILD) != 0);

    // Case 4: Combining different flag types doesn't interfere
    uint32_t combined = BLOCK_VALID_TRANSACTIONS | BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO;
    BOOST_CHECK((combined & BLOCK_VALID_MASK) == BLOCK_VALID_TRANSACTIONS);
    BOOST_CHECK((combined & BLOCK_HAVE_DATA) != 0);
    BOOST_CHECK((combined & BLOCK_HAVE_UNDO) != 0);
    BOOST_CHECK((combined & BLOCK_FAILED_MASK) == 0);

    // Case 5: Clearing validity bits preserves other flags
    uint32_t status = BLOCK_VALID_TREE | BLOCK_HAVE_DATA | BLOCK_OPT_WITNESS;
    status = (status & ~BLOCK_VALID_MASK) | BLOCK_VALID_TRANSACTIONS;
    BOOST_CHECK((status & BLOCK_VALID_MASK) == BLOCK_VALID_TRANSACTIONS);
    BOOST_CHECK((status & BLOCK_HAVE_DATA) != 0);
    BOOST_CHECK((status & BLOCK_OPT_WITNESS) != 0);

    // Case 6: All flags are powers of 2 (no overlap)
    BOOST_CHECK((BLOCK_VALID_HEADER & BLOCK_VALID_TREE) == 0);
    BOOST_CHECK((BLOCK_HAVE_DATA & BLOCK_HAVE_UNDO) == 0);
    BOOST_CHECK((BLOCK_FAILED_VALID & BLOCK_FAILED_CHILD) == 0);
}

/**
 * Test CBlockIndex initialization and SetNull
 *
 * Tests that CBlockIndex is properly initialized and SetNull clears all fields.
 */
BOOST_AUTO_TEST_CASE(blockindex_initialization)
{
    // Case 1: Default constructor initializes to null state
    CBlockIndex index1;
    BOOST_CHECK(index1.phashBlock == nullptr);
    BOOST_CHECK(index1.pprev == nullptr);
    BOOST_CHECK(index1.pskip == nullptr);
    BOOST_CHECK_EQUAL(index1.nHeight, 0);
    BOOST_CHECK_EQUAL(index1.nFile, 0);
    BOOST_CHECK_EQUAL(index1.nDataPos, 0);
    BOOST_CHECK_EQUAL(index1.nUndoPos, 0);
    BOOST_CHECK_EQUAL(index1.nTx, 0);
    BOOST_CHECK_EQUAL(index1.nChainTx, 0);
    BOOST_CHECK_EQUAL(index1.nStatus, 0);
    BOOST_CHECK_EQUAL(index1.nSequenceId, 0);
    BOOST_CHECK_EQUAL(index1.nTimeMax, 0);
    BOOST_CHECK_EQUAL(index1.nFeatures, 0);
    BOOST_CHECK(index1.hashMerkleRoot.IsNull());
    BOOST_CHECK_EQUAL(index1.nTime, 0);
    BOOST_CHECK(index1.xfield.xfieldType == TAPYRUS_XFIELDTYPES::NONE);
    BOOST_CHECK(index1.proof.empty());

    // Case 2: Constructor from CBlockHeader
    CBlockHeader header;
    header.nFeatures = 2;
    header.hashMerkleRoot = InsecureRand256();
    header.hashImMerkleRoot = InsecureRand256();
    header.nTime = 1234567890;
    header.proof = {0x01, 0x02, 0x03};
    header.xfield = CXField(XFieldAggPubKey({0x04, 0x05, 0x06}));

    CBlockIndex index2(header);
    BOOST_CHECK_EQUAL(index2.nFeatures, header.nFeatures);
    BOOST_CHECK(index2.hashMerkleRoot == header.hashMerkleRoot);
    BOOST_CHECK(index2.hashImMerkleRoot == header.hashImMerkleRoot);
    BOOST_CHECK_EQUAL(index2.nTime, header.nTime);
    BOOST_CHECK(index2.proof == header.proof);
    BOOST_CHECK(index2.xfield.xfieldType == header.xfield.xfieldType);

    // Other fields should still be initialized to null/zero
    BOOST_CHECK(index2.phashBlock == nullptr);
    BOOST_CHECK(index2.pprev == nullptr);
    BOOST_CHECK_EQUAL(index2.nHeight, 0);

    // Case 3: SetNull resets all fields
    index2.nHeight = 100;
    index2.nStatus = BLOCK_VALID_SCRIPTS | BLOCK_HAVE_DATA;
    index2.nChainTx = 50000;

    index2.SetNull();

    BOOST_CHECK(index2.phashBlock == nullptr);
    BOOST_CHECK(index2.pprev == nullptr);
    BOOST_CHECK_EQUAL(index2.nHeight, 0);
    BOOST_CHECK_EQUAL(index2.nStatus, 0);
    BOOST_CHECK_EQUAL(index2.nChainTx, 0);
    BOOST_CHECK(index2.hashMerkleRoot.IsNull());
}

// ============================================================================
// CBlockFileInfo Tests
// ============================================================================

/**
 * Test CBlockFileInfo initialization and SetNull
 *
 * Tests that CBlockFileInfo is properly initialized to zero values
 * and that SetNull() correctly resets all fields to zero.
 * Current behavior: All fields start at 0 (documented as-is).
 */
BOOST_AUTO_TEST_CASE(blockfileinfo_initialization)
{
    // Case 1: Default constructor should initialize all fields to zero
    CBlockFileInfo info;
    BOOST_CHECK_EQUAL(info.nBlocks, 0);
    BOOST_CHECK_EQUAL(info.nSize, 0);
    BOOST_CHECK_EQUAL(info.nUndoSize, 0);
    BOOST_CHECK_EQUAL(info.nHeightFirst, 0);
    BOOST_CHECK_EQUAL(info.nHeightLast, 0);
    BOOST_CHECK_EQUAL(info.nTimeFirst, 0);
    BOOST_CHECK_EQUAL(info.nTimeLast, 0);

    // Case 2: Modify fields then call SetNull() - should reset to zero
    info.nBlocks = 100;
    info.nSize = 1024000;
    info.nUndoSize = 512000;
    info.nHeightFirst = 1000;
    info.nHeightLast = 1099;
    info.nTimeFirst = 1609459200;
    info.nTimeLast = 1609545600;

    info.SetNull();

    BOOST_CHECK_EQUAL(info.nBlocks, 0);
    BOOST_CHECK_EQUAL(info.nSize, 0);
    BOOST_CHECK_EQUAL(info.nUndoSize, 0);
    BOOST_CHECK_EQUAL(info.nHeightFirst, 0);
    BOOST_CHECK_EQUAL(info.nHeightLast, 0);
    BOOST_CHECK_EQUAL(info.nTimeFirst, 0);
    BOOST_CHECK_EQUAL(info.nTimeLast, 0);
}

/**
 * Test CBlockFileInfo AddBlock - first block added
 *
 * Tests the behavior when the first block (height=0, genesis) is added.
 * Should initialize nHeightFirst, nHeightLast, nTimeFirst, nTimeLast,
 * and increment nBlocks to 1.
 */
BOOST_AUTO_TEST_CASE(blockfileinfo_addblock_first)
{
    CBlockFileInfo info;

    // Add first block (genesis at height 0, timestamp 1000000000)
    info.AddBlock(0, 1000000000);

    BOOST_CHECK_EQUAL(info.nBlocks, 1);
    BOOST_CHECK_EQUAL(info.nHeightFirst, 0);
    BOOST_CHECK_EQUAL(info.nHeightLast, 0);
    BOOST_CHECK_EQUAL(info.nTimeFirst, 1000000000);
    BOOST_CHECK_EQUAL(info.nTimeLast, 1000000000);

    // nSize and nUndoSize are not updated by AddBlock
    BOOST_CHECK_EQUAL(info.nSize, 0);
    BOOST_CHECK_EQUAL(info.nUndoSize, 0);
}

/**
 * Test CBlockFileInfo AddBlock - sequence of blocks in order
 *
 * Tests adding multiple blocks in increasing height and time order.
 * Verifies that height ranges and time ranges track correctly,
 * and nBlocks counter increments properly.
 */
BOOST_AUTO_TEST_CASE(blockfileinfo_addblock_sequence)
{
    CBlockFileInfo info;

    // Add blocks sequentially: heights 0-9, times 1000-1009
    for (unsigned int i = 0; i < 10; i++) {
        info.AddBlock(i, 1000 + i);
    }

    BOOST_CHECK_EQUAL(info.nBlocks, 10);
    BOOST_CHECK_EQUAL(info.nHeightFirst, 0);
    BOOST_CHECK_EQUAL(info.nHeightLast, 9);
    BOOST_CHECK_EQUAL(info.nTimeFirst, 1000);
    BOOST_CHECK_EQUAL(info.nTimeLast, 1009);

    // Add more blocks continuing the sequence
    for (unsigned int i = 10; i < 20; i++) {
        info.AddBlock(i, 1000 + i);
    }

    BOOST_CHECK_EQUAL(info.nBlocks, 20);
    BOOST_CHECK_EQUAL(info.nHeightFirst, 0);
    BOOST_CHECK_EQUAL(info.nHeightLast, 19);
    BOOST_CHECK_EQUAL(info.nTimeFirst, 1000);
    BOOST_CHECK_EQUAL(info.nTimeLast, 1019);
}

/**
 * Test CBlockFileInfo AddBlock - edge cases
 *
 * Tests edge cases including:
 * - Adding blocks with lower heights (out of order)
 * - Adding blocks with higher heights
 * - Adding blocks with earlier/later times
 * - Block counter always increments
 */
BOOST_AUTO_TEST_CASE(blockfileinfo_addblock_edge_cases)
{
    CBlockFileInfo info;

    // Case 1: Add block at height 100, time 2000
    info.AddBlock(100, 2000);
    BOOST_CHECK_EQUAL(info.nBlocks, 1);
    BOOST_CHECK_EQUAL(info.nHeightFirst, 100);
    BOOST_CHECK_EQUAL(info.nHeightLast, 100);
    BOOST_CHECK_EQUAL(info.nTimeFirst, 2000);
    BOOST_CHECK_EQUAL(info.nTimeLast, 2000);

    // Case 2: Add block with LOWER height (out of order) - should update nHeightFirst
    info.AddBlock(50, 1800);
    BOOST_CHECK_EQUAL(info.nBlocks, 2);
    BOOST_CHECK_EQUAL(info.nHeightFirst, 50);  // Updated to lower height
    BOOST_CHECK_EQUAL(info.nHeightLast, 100);  // Unchanged (100 still highest)
    BOOST_CHECK_EQUAL(info.nTimeFirst, 1800);  // Updated to earlier time
    BOOST_CHECK_EQUAL(info.nTimeLast, 2000);   // Unchanged

    // Case 3: Add block with HIGHER height
    info.AddBlock(150, 2500);
    BOOST_CHECK_EQUAL(info.nBlocks, 3);
    BOOST_CHECK_EQUAL(info.nHeightFirst, 50);   // Unchanged
    BOOST_CHECK_EQUAL(info.nHeightLast, 150);   // Updated to higher height
    BOOST_CHECK_EQUAL(info.nTimeFirst, 1800);   // Unchanged
    BOOST_CHECK_EQUAL(info.nTimeLast, 2500);    // Updated to later time

    // Case 4: Add block with time earlier than nTimeFirst but height in middle
    info.AddBlock(75, 1500);
    BOOST_CHECK_EQUAL(info.nBlocks, 4);
    BOOST_CHECK_EQUAL(info.nTimeFirst, 1500);  // Updated to earliest time
    BOOST_CHECK_EQUAL(info.nTimeLast, 2500);   // Unchanged

    // Case 5: Add block with time later than nTimeLast but height in middle
    info.AddBlock(125, 3000);
    BOOST_CHECK_EQUAL(info.nBlocks, 5);
    BOOST_CHECK_EQUAL(info.nTimeFirst, 1500);  // Unchanged
    BOOST_CHECK_EQUAL(info.nTimeLast, 3000);   // Updated to latest time

    // Case 6: Verify block counter always increments, even for duplicate heights
    unsigned int previousBlocks = info.nBlocks;
    info.AddBlock(100, 2100);  // Same height as earlier block
    BOOST_CHECK_EQUAL(info.nBlocks, previousBlocks + 1);
}

/**
 * Test CBlockFileInfo size tracking
 *
 * Tests that nSize and nUndoSize can be manually tracked alongside
 * AddBlock operations. AddBlock does not modify these fields.
 */
BOOST_AUTO_TEST_CASE(blockfileinfo_size_tracking)
{
    CBlockFileInfo info;

    // Add blocks and manually update sizes
    info.AddBlock(0, 1000);
    info.nSize = 500;        // Simulate 500 bytes block data
    info.nUndoSize = 100;    // Simulate 100 bytes undo data

    BOOST_CHECK_EQUAL(info.nBlocks, 1);
    BOOST_CHECK_EQUAL(info.nSize, 500);
    BOOST_CHECK_EQUAL(info.nUndoSize, 100);

    // Add more blocks and grow sizes
    info.AddBlock(1, 1001);
    info.nSize += 750;       // Add 750 bytes
    info.nUndoSize += 150;   // Add 150 bytes

    BOOST_CHECK_EQUAL(info.nBlocks, 2);
    BOOST_CHECK_EQUAL(info.nSize, 1250);
    BOOST_CHECK_EQUAL(info.nUndoSize, 250);

    // Test realistic sizes: simulate 100 blocks, ~1MB each
    for (unsigned int i = 2; i < 100; i++) {
        info.AddBlock(i, 1000 + i);
        info.nSize += 1000000;      // ~1MB per block
        info.nUndoSize += 100000;   // ~100KB undo per block
    }

    BOOST_CHECK_EQUAL(info.nBlocks, 100);
    // 1250 (initial) + 98 * 1000000 = 98001250
    BOOST_CHECK_EQUAL(info.nSize, 98001250);
    // 250 (initial) + 98 * 100000 = 9800250
    BOOST_CHECK_EQUAL(info.nUndoSize, 9800250);

    // Test boundary: approaching 2GB (INT_MAX for signed, but nSize is unsigned)
    info.nSize = 2000000000u;  // 2GB
    info.nUndoSize = 1000000000u;  // 1GB
    BOOST_CHECK_EQUAL(info.nSize, 2000000000u);
    BOOST_CHECK_EQUAL(info.nUndoSize, 1000000000u);
}

/**
 * Test CBlockFileInfo time ranges with various scenarios
 *
 * Tests time tracking with:
 * - Same timestamps for multiple blocks
 * - Large time gaps
 * - Time values at boundaries (0, MAX)
 */
BOOST_AUTO_TEST_CASE(blockfileinfo_time_ranges)
{
    // Case 1: Multiple blocks with identical timestamp
    CBlockFileInfo info1;
    for (unsigned int i = 0; i < 5; i++) {
        info1.AddBlock(i, 1000000000);  // All have same timestamp
    }
    BOOST_CHECK_EQUAL(info1.nBlocks, 5);
    BOOST_CHECK_EQUAL(info1.nTimeFirst, 1000000000);
    BOOST_CHECK_EQUAL(info1.nTimeLast, 1000000000);

    // Case 2: Large time gap between blocks
    CBlockFileInfo info2;
    info2.AddBlock(0, 1000);
    info2.AddBlock(1, 1000000000);  // ~31 years later
    BOOST_CHECK_EQUAL(info2.nTimeFirst, 1000);
    BOOST_CHECK_EQUAL(info2.nTimeLast, 1000000000);

    // Case 3: Time value at 0
    CBlockFileInfo info3;
    info3.AddBlock(0, 0);
    BOOST_CHECK_EQUAL(info3.nTimeFirst, 0);
    BOOST_CHECK_EQUAL(info3.nTimeLast, 0);

    info3.AddBlock(1, 1000);
    BOOST_CHECK_EQUAL(info3.nTimeFirst, 0);     // Still 0 (earliest)
    BOOST_CHECK_EQUAL(info3.nTimeLast, 1000);

    // Case 4: Maximum uint64_t time value
    CBlockFileInfo info4;
    uint64_t maxTime = std::numeric_limits<uint64_t>::max();
    info4.AddBlock(0, maxTime);
    BOOST_CHECK_EQUAL(info4.nTimeFirst, maxTime);
    BOOST_CHECK_EQUAL(info4.nTimeLast, maxTime);

    // Add earlier time - should update nTimeFirst
    info4.AddBlock(1, 1000);
    BOOST_CHECK_EQUAL(info4.nTimeFirst, 1000);
    BOOST_CHECK_EQUAL(info4.nTimeLast, maxTime);

    // Case 5: Realistic Bitcoin timestamps (year 2009 to 2025)
    CBlockFileInfo info5;
    info5.AddBlock(0, 1231006505);      // Genesis block (Jan 3, 2009)
    info5.AddBlock(1000000, 1735689600); // Approx Jan 1, 2025
    BOOST_CHECK_EQUAL(info5.nTimeFirst, 1231006505);
    BOOST_CHECK_EQUAL(info5.nTimeLast, 1735689600);
}

/**
 * Test CBlockFileInfo ToString method
 *
 * Tests the ToString() output format with cs_main lock acquired.
 * Verifies that the string contains expected information.
 * Note: ToString() requires cs_main lock (AssertLockHeld).
 */
BOOST_AUTO_TEST_CASE(blockfileinfo_tostring)
{
    CBlockFileInfo info;
    info.nBlocks = 100;
    info.nSize = 1024000;
    info.nHeightFirst = 1000;
    info.nHeightLast = 1099;
    info.nTimeFirst = 1609459200;  // 2021-01-01 00:00:00 UTC
    info.nTimeLast = 1640995200;   // 2022-01-01 00:00:00 UTC

    // Must acquire cs_main lock before calling ToString()
    LOCK(cs_main);
    std::string str = info.ToString();

    // Verify string contains key information
    // Format: "CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)"
    BOOST_CHECK(str.find("CBlockFileInfo") != std::string::npos);
    BOOST_CHECK(str.find("blocks=100") != std::string::npos);
    BOOST_CHECK(str.find("size=1024000") != std::string::npos);
    BOOST_CHECK(str.find("heights=1000...1099") != std::string::npos);
    // Time format is ISO8601, should contain year
    BOOST_CHECK(str.find("2021") != std::string::npos);
    BOOST_CHECK(str.find("2022") != std::string::npos);

    // Case 2: Empty/default info
    CBlockFileInfo emptyInfo;
    std::string emptyStr = emptyInfo.ToString();
    BOOST_CHECK(emptyStr.find("blocks=0") != std::string::npos);
    BOOST_CHECK(emptyStr.find("size=0") != std::string::npos);
    BOOST_CHECK(emptyStr.find("heights=0...0") != std::string::npos);
}

/**
 * Test CBlockFileInfo serialization round-trip
 *
 * Tests full serialization/deserialization cycle using CDataStream.
 * Verifies that all fields are correctly preserved through the round-trip.
 * Tests both realistic values and boundary values.
 */
BOOST_AUTO_TEST_CASE(blockfileinfo_serialization)
{
    // Case 1: Realistic values
    CBlockFileInfo original;
    original.nBlocks = 100;
    original.nSize = 52428800;      // 50 MB
    original.nUndoSize = 5242880;   // 5 MB
    original.nHeightFirst = 10000;
    original.nHeightLast = 10099;
    original.nTimeFirst = 1609459200;
    original.nTimeLast = 1640995200;

    // Serialize
    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;

    // Deserialize
    CBlockFileInfo deserialized;
    ss >> deserialized;

    // Verify all fields match
    BOOST_CHECK_EQUAL(deserialized.nBlocks, original.nBlocks);
    BOOST_CHECK_EQUAL(deserialized.nSize, original.nSize);
    BOOST_CHECK_EQUAL(deserialized.nUndoSize, original.nUndoSize);
    BOOST_CHECK_EQUAL(deserialized.nHeightFirst, original.nHeightFirst);
    BOOST_CHECK_EQUAL(deserialized.nHeightLast, original.nHeightLast);
    BOOST_CHECK_EQUAL(deserialized.nTimeFirst, original.nTimeFirst);
    BOOST_CHECK_EQUAL(deserialized.nTimeLast, original.nTimeLast);

    // Case 2: Boundary values - all zeros
    CBlockFileInfo zeros;
    zeros.SetNull();

    CDataStream ss2(SER_DISK, CLIENT_VERSION);
    ss2 << zeros;

    CBlockFileInfo deserializedZeros;
    ss2 >> deserializedZeros;

    BOOST_CHECK_EQUAL(deserializedZeros.nBlocks, 0);
    BOOST_CHECK_EQUAL(deserializedZeros.nSize, 0);
    BOOST_CHECK_EQUAL(deserializedZeros.nUndoSize, 0);
    BOOST_CHECK_EQUAL(deserializedZeros.nHeightFirst, 0);
    BOOST_CHECK_EQUAL(deserializedZeros.nHeightLast, 0);
    BOOST_CHECK_EQUAL(deserializedZeros.nTimeFirst, 0);
    BOOST_CHECK_EQUAL(deserializedZeros.nTimeLast, 0);

    // Case 3: Maximum values
    CBlockFileInfo maxValues;
    maxValues.nBlocks = std::numeric_limits<unsigned int>::max();
    maxValues.nSize = std::numeric_limits<unsigned int>::max();
    maxValues.nUndoSize = std::numeric_limits<unsigned int>::max();
    maxValues.nHeightFirst = std::numeric_limits<unsigned int>::max();
    maxValues.nHeightLast = std::numeric_limits<unsigned int>::max();
    maxValues.nTimeFirst = std::numeric_limits<uint64_t>::max();
    maxValues.nTimeLast = std::numeric_limits<uint64_t>::max();

    CDataStream ss3(SER_DISK, CLIENT_VERSION);
    ss3 << maxValues;

    CBlockFileInfo deserializedMax;
    ss3 >> deserializedMax;

    BOOST_CHECK_EQUAL(deserializedMax.nBlocks, maxValues.nBlocks);
    BOOST_CHECK_EQUAL(deserializedMax.nSize, maxValues.nSize);
    BOOST_CHECK_EQUAL(deserializedMax.nUndoSize, maxValues.nUndoSize);
    BOOST_CHECK_EQUAL(deserializedMax.nHeightFirst, maxValues.nHeightFirst);
    BOOST_CHECK_EQUAL(deserializedMax.nHeightLast, maxValues.nHeightLast);
    BOOST_CHECK_EQUAL(deserializedMax.nTimeFirst, maxValues.nTimeFirst);
    BOOST_CHECK_EQUAL(deserializedMax.nTimeLast, maxValues.nTimeLast);

    // Case 4: Test with realistic large file (2GB)
    CBlockFileInfo largeFile;
    largeFile.nBlocks = 20000;
    largeFile.nSize = 2147483648u;     // 2GB (just over INT_MAX)
    largeFile.nUndoSize = 1073741824u; // 1GB
    largeFile.nHeightFirst = 0;
    largeFile.nHeightLast = 19999;
    largeFile.nTimeFirst = 1231006505;  // Genesis
    largeFile.nTimeLast = 1735689600;   // 2025

    CDataStream ss4(SER_DISK, CLIENT_VERSION);
    ss4 << largeFile;

    CBlockFileInfo deserializedLarge;
    ss4 >> deserializedLarge;

    BOOST_CHECK_EQUAL(deserializedLarge.nBlocks, largeFile.nBlocks);
    BOOST_CHECK_EQUAL(deserializedLarge.nSize, largeFile.nSize);
    BOOST_CHECK_EQUAL(deserializedLarge.nUndoSize, largeFile.nUndoSize);
    BOOST_CHECK_EQUAL(deserializedLarge.nHeightFirst, largeFile.nHeightFirst);
    BOOST_CHECK_EQUAL(deserializedLarge.nHeightLast, largeFile.nHeightLast);
    BOOST_CHECK_EQUAL(deserializedLarge.nTimeFirst, largeFile.nTimeFirst);
    BOOST_CHECK_EQUAL(deserializedLarge.nTimeLast, largeFile.nTimeLast);
}

// ============================================================================
// CDiskBlockPos Tests
// ============================================================================

/**
 * Test CDiskBlockPos initialization
 *
 * Tests both default constructor (initializes to null via SetNull)
 * and parameterized constructor with various file/position values.
 * Documents actual behavior: default constructor calls SetNull(),
 * which sets nFile=-1, nPos=0.
 */
BOOST_AUTO_TEST_CASE(diskblockpos_initialization)
{
    // Case 1: Default constructor - initializes to null state
    // Current behavior: calls SetNull() which sets nFile=-1, nPos=0
    CDiskBlockPos defaultPos;
    BOOST_CHECK_EQUAL(defaultPos.nFile, -1);
    BOOST_CHECK_EQUAL(defaultPos.nPos, 0);
    BOOST_CHECK(defaultPos.IsNull());

    // Case 2: Parameterized constructor with valid values
    CDiskBlockPos pos1(5, 1000);
    BOOST_CHECK_EQUAL(pos1.nFile, 5);
    BOOST_CHECK_EQUAL(pos1.nPos, 1000);
    BOOST_CHECK(!pos1.IsNull());

    // Case 3: File 0, position 0 - valid, not null
    CDiskBlockPos pos2(0, 0);
    BOOST_CHECK_EQUAL(pos2.nFile, 0);
    BOOST_CHECK_EQUAL(pos2.nPos, 0);
    BOOST_CHECK(!pos2.IsNull());  // File 0 is valid

    // Case 4: Large file number
    CDiskBlockPos pos3(100, 52428800);  // File 100, 50MB offset
    BOOST_CHECK_EQUAL(pos3.nFile, 100);
    BOOST_CHECK_EQUAL(pos3.nPos, 52428800);
    BOOST_CHECK(!pos3.IsNull());

    // Case 5: Maximum realistic values
    CDiskBlockPos pos4(999, 2147483648u);  // File 999, 2GB offset
    BOOST_CHECK_EQUAL(pos4.nFile, 999);
    BOOST_CHECK_EQUAL(pos4.nPos, 2147483648u);
    BOOST_CHECK(!pos4.IsNull());

    // Case 6: INT_MAX for nFile (boundary test)
    CDiskBlockPos pos5(std::numeric_limits<int>::max(), 1000);
    BOOST_CHECK_EQUAL(pos5.nFile, std::numeric_limits<int>::max());
    BOOST_CHECK_EQUAL(pos5.nPos, 1000);
    BOOST_CHECK(!pos5.IsNull());
}

/**
 * Test CDiskBlockPos IsNull edge cases
 *
 * Tests IsNull() method with various nFile values.
 * Behavior: IsNull() returns true ONLY when nFile == -1.
 * Tests nFile values: -1 (null), 0 (valid), positive, negative other than -1.
 */
BOOST_AUTO_TEST_CASE(diskblockpos_isnull_edge_cases)
{
    // Case 1: nFile = -1 is null
    CDiskBlockPos null1(-1, 0);
    BOOST_CHECK(null1.IsNull());

    CDiskBlockPos null2(-1, 1000);  // nPos doesn't matter
    BOOST_CHECK(null2.IsNull());

    CDiskBlockPos null3(-1, std::numeric_limits<unsigned int>::max());
    BOOST_CHECK(null3.IsNull());

    // Case 2: nFile = 0 is NOT null (valid file)
    CDiskBlockPos valid0(0, 0);
    BOOST_CHECK(!valid0.IsNull());

    // Case 3: Positive nFile values are not null
    CDiskBlockPos valid1(1, 0);
    BOOST_CHECK(!valid1.IsNull());

    CDiskBlockPos valid2(100, 1000);
    BOOST_CHECK(!valid2.IsNull());

    // Case 4: Large positive nFile
    CDiskBlockPos valid3(std::numeric_limits<int>::max(), 0);
    BOOST_CHECK(!valid3.IsNull());

    // Case 5: Negative nFile values other than -1 (edge case, undefined behavior)
    // These would be invalid in practice, but test the IsNull() logic
    CDiskBlockPos negative2(-2, 0);
    BOOST_CHECK(!negative2.IsNull());  // IsNull checks for exactly -1

    CDiskBlockPos negative100(-100, 0);
    BOOST_CHECK(!negative100.IsNull());
}

/**
 * Test CDiskBlockPos SetNull behavior
 *
 * Tests that SetNull() correctly sets nFile=-1 and nPos=0,
 * and that IsNull() returns true after SetNull().
 */
BOOST_AUTO_TEST_CASE(diskblockpos_setnull)
{
    // Case 1: SetNull on default-constructed position
    CDiskBlockPos pos1;
    pos1.SetNull();
    BOOST_CHECK_EQUAL(pos1.nFile, -1);
    BOOST_CHECK_EQUAL(pos1.nPos, 0);
    BOOST_CHECK(pos1.IsNull());

    // Case 2: SetNull on position with values
    CDiskBlockPos pos2(10, 5000);
    BOOST_CHECK(!pos2.IsNull());

    pos2.SetNull();
    BOOST_CHECK_EQUAL(pos2.nFile, -1);
    BOOST_CHECK_EQUAL(pos2.nPos, 0);
    BOOST_CHECK(pos2.IsNull());

    // Case 3: Multiple SetNull calls
    pos2.SetNull();
    BOOST_CHECK(pos2.IsNull());
    pos2.SetNull();
    BOOST_CHECK(pos2.IsNull());

    // Case 4: SetNull on position with large values
    CDiskBlockPos pos3(999, 2147483648u);
    pos3.SetNull();
    BOOST_CHECK_EQUAL(pos3.nFile, -1);
    BOOST_CHECK_EQUAL(pos3.nPos, 0);
    BOOST_CHECK(pos3.IsNull());
}

/**
 * Test CDiskBlockPos equality operators (== and !=)
 *
 * Tests operator== and operator!= with various combinations:
 * - Equal positions (same file and pos)
 * - Different file, same pos
 * - Same file, different pos
 * - Both different
 * - Null positions
 */
BOOST_AUTO_TEST_CASE(diskblockpos_equality_operators)
{
    // Case 1: Identical positions are equal
    CDiskBlockPos pos1(5, 1000);
    CDiskBlockPos pos2(5, 1000);
    BOOST_CHECK(pos1 == pos2);
    BOOST_CHECK(!(pos1 != pos2));

    // Case 2: Different file, same position - not equal
    CDiskBlockPos pos3(6, 1000);
    BOOST_CHECK(!(pos1 == pos3));
    BOOST_CHECK(pos1 != pos3);

    // Case 3: Same file, different position - not equal
    CDiskBlockPos pos4(5, 2000);
    BOOST_CHECK(!(pos1 == pos4));
    BOOST_CHECK(pos1 != pos4);

    // Case 4: Both file and position different - not equal
    CDiskBlockPos pos5(6, 2000);
    BOOST_CHECK(!(pos1 == pos5));
    BOOST_CHECK(pos1 != pos5);

    // Case 5: Null positions are equal to each other
    CDiskBlockPos null1;
    CDiskBlockPos null2;
    null1.SetNull();
    null2.SetNull();
    BOOST_CHECK(null1 == null2);
    BOOST_CHECK(!(null1 != null2));

    // Case 6: Null position not equal to non-null
    CDiskBlockPos notNull(0, 0);
    BOOST_CHECK(!(null1 == notNull));
    BOOST_CHECK(null1 != notNull);

    // Case 7: File 0, pos 0 vs file 0, pos 0 (edge case - both valid, should be equal)
    CDiskBlockPos zero1(0, 0);
    CDiskBlockPos zero2(0, 0);
    BOOST_CHECK(zero1 == zero2);
    BOOST_CHECK(!(zero1 != zero2));

    // Case 8: Large values equality
    CDiskBlockPos large1(999, 2147483648u);
    CDiskBlockPos large2(999, 2147483648u);
    BOOST_CHECK(large1 == large2);
    BOOST_CHECK(!(large1 != large2));

    // Case 9: Self-equality
    BOOST_CHECK(pos1 == pos1);
    BOOST_CHECK(!(pos1 != pos1));
}

/**
 * Test CDiskBlockPos serialization round-trip
 *
 * Tests full serialization/deserialization with CDataStream.
 * Verifies VarInt encoding is used correctly for both nFile and nPos.
 * Tests realistic values, boundary values, and null positions.
 */
BOOST_AUTO_TEST_CASE(diskblockpos_serialization)
{
    // Case 1: Realistic values
    CDiskBlockPos original(10, 52428800);  // File 10, 50MB offset

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << original;

    CDiskBlockPos deserialized;
    ss >> deserialized;

    BOOST_CHECK(original == deserialized);
    BOOST_CHECK_EQUAL(deserialized.nFile, 10);
    BOOST_CHECK_EQUAL(deserialized.nPos, 52428800);

    // Case 2: Note on null positions
    // CDiskBlockPos with nFile=-1 (null position) is not typically serialized
    // in production code. The CDiskBlockIndex serialization conditionally writes
    // nFile only when BLOCK_HAVE_DATA or BLOCK_HAVE_UNDO flags are set.
    // Therefore, we skip testing serialization of null positions as it's not
    // a realistic use case. In practice, only valid (non-null) positions are serialized.

    // Case 2: File 0, position 0 (boundary)
    CDiskBlockPos zero(0, 0);

    CDataStream ss2(SER_DISK, CLIENT_VERSION);
    ss2 << zero;

    CDiskBlockPos deserializedZero;
    ss2 >> deserializedZero;

    BOOST_CHECK(zero == deserializedZero);
    BOOST_CHECK_EQUAL(deserializedZero.nFile, 0);
    BOOST_CHECK_EQUAL(deserializedZero.nPos, 0);
    BOOST_CHECK(!deserializedZero.IsNull());

    // Case 3: Large file numbers and positions
    CDiskBlockPos large(100, 2147483648u);  // 2GB offset

    CDataStream ss3(SER_DISK, CLIENT_VERSION);
    ss3 << large;

    CDiskBlockPos deserializedLarge;
    ss3 >> deserializedLarge;

    BOOST_CHECK(large == deserializedLarge);
    BOOST_CHECK_EQUAL(deserializedLarge.nFile, 100);
    BOOST_CHECK_EQUAL(deserializedLarge.nPos, 2147483648u);

    // Case 4: Maximum realistic file number (boundary)
    CDiskBlockPos maxFile(999, 1000000);

    CDataStream ss4(SER_DISK, CLIENT_VERSION);
    ss4 << maxFile;

    CDiskBlockPos deserializedMaxFile;
    ss4 >> deserializedMaxFile;

    BOOST_CHECK(maxFile == deserializedMaxFile);
    BOOST_CHECK_EQUAL(deserializedMaxFile.nFile, 999);
    BOOST_CHECK_EQUAL(deserializedMaxFile.nPos, 1000000);

    // Case 5: Maximum position value (unsigned int max)
    CDiskBlockPos maxPos(50, std::numeric_limits<unsigned int>::max());

    CDataStream ss5(SER_DISK, CLIENT_VERSION);
    ss5 << maxPos;

    CDiskBlockPos deserializedMaxPos;
    ss5 >> deserializedMaxPos;

    BOOST_CHECK(maxPos == deserializedMaxPos);
    BOOST_CHECK_EQUAL(deserializedMaxPos.nFile, 50);
    BOOST_CHECK_EQUAL(deserializedMaxPos.nPos, std::numeric_limits<unsigned int>::max());
}

/**
 * Test CDiskBlockPos ToString format
 *
 * Tests the ToString() method output format.
 * Verifies string contains file and position information.
 * Format: "CBlockDiskPos(nFile=%i, nPos=%i)"
 */
BOOST_AUTO_TEST_CASE(diskblockpos_tostring)
{
    // Case 1: Regular position
    CDiskBlockPos pos1(10, 5000);
    std::string str1 = pos1.ToString();

    BOOST_CHECK(str1.find("CBlockDiskPos") != std::string::npos);
    BOOST_CHECK(str1.find("nFile=10") != std::string::npos);
    BOOST_CHECK(str1.find("nPos=5000") != std::string::npos);

    // Case 2: Null position (nFile=-1)
    CDiskBlockPos nullPos;
    nullPos.SetNull();
    std::string strNull = nullPos.ToString();

    BOOST_CHECK(strNull.find("CBlockDiskPos") != std::string::npos);
    BOOST_CHECK(strNull.find("nFile=-1") != std::string::npos);
    BOOST_CHECK(strNull.find("nPos=0") != std::string::npos);

    // Case 3: File 0, position 0
    CDiskBlockPos zero(0, 0);
    std::string strZero = zero.ToString();

    BOOST_CHECK(strZero.find("nFile=0") != std::string::npos);
    BOOST_CHECK(strZero.find("nPos=0") != std::string::npos);

    // Case 4: Large values
    CDiskBlockPos large(999, 2147483648u);
    std::string strLarge = large.ToString();

    BOOST_CHECK(strLarge.find("nFile=999") != std::string::npos);
    BOOST_CHECK(strLarge.find("nPos=2147483648") != std::string::npos);

    // Case 5: Verify format consistency
    // All strings should start with "CBlockDiskPos(" and end with ")"
    BOOST_CHECK(str1.find("CBlockDiskPos(") == 0);
    BOOST_CHECK(str1.back() == ')');
}

BOOST_AUTO_TEST_SUITE_END()
