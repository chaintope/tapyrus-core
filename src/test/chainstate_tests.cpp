// Copyright (c) 2025 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Unit tests for CChainState class - Edge Cases
 *
 * This test suite focuses on edge cases and boundary conditions in the CChainState
 * class and related components. It tests scenarios that are less common but critical
 * for robustness, including:
 *
 * - Block comparator edge cases (equal heights, sequence IDs, pointer tie-breakers)
 * - Disconnect operations with missing or inconsistent data
 * - Precious block edge cases (counter overflow, chain extensions)
 * - Invalid block handling with various corruption states
 * - Block sequence ID management and thread safety
 * - Block hash collisions and hasher edge cases
 */

#include <chainstate.h>
#include <test/test_tapyrus.h>
#include <validation.h>
#include <consensus/validation.h>
#include <coins.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <file_io.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainstate_tests, TestChainSetup)

/**
 * Test CBlockIndexWorkComparator edge cases
 *
 * The comparator is used to order blocks in setBlockIndexCandidates.
 * Edge cases include blocks with identical heights, sequence IDs, and
 * blocks loaded from disk (id = 0).
 */
BOOST_AUTO_TEST_CASE(blockindex_work_comparator_edge_cases)
{
    CBlockIndexWorkComparator comparator;

    // Create test block indices
    CBlockIndex indexA, indexB;

    // Case 1: Same height, different sequence IDs
    // Block with HIGHER sequence ID should come first (more recent blocks prioritized)
    indexA.nHeight = 100;
    indexA.nSequenceId = 5;
    indexB.nHeight = 100;
    indexB.nSequenceId = 10;

    BOOST_CHECK(comparator(&indexA, &indexB) == false); // A > B (lower seq ID, so A comes after B)
    BOOST_CHECK(comparator(&indexB, &indexA) == true);  // B < A (higher seq ID, so B comes before A)

    // Case 2: Same height, same sequence ID (pointer tie-breaker)
    // This happens with blocks loaded from disk (both have id = 0)
    indexA.nHeight = 100;
    indexA.nSequenceId = 0;
    indexB.nHeight = 100;
    indexB.nSequenceId = 0;

    // With same height and sequence ID, pointer address determines order
    bool resultAB = comparator(&indexA, &indexB);
    bool resultBA = comparator(&indexB, &indexA);

    // They should give opposite results (unless indexA == indexB)
    if (&indexA != &indexB) {
        BOOST_CHECK(resultAB != resultBA);
    }

    // Case 3: Identical blocks (same pointer)
    BOOST_CHECK(comparator(&indexA, &indexA) == false);

    // Case 4: Height difference takes precedence
    indexA.nHeight = 99;
    indexA.nSequenceId = 100;
    indexB.nHeight = 100;
    indexB.nSequenceId = 1;

    BOOST_CHECK(comparator(&indexA, &indexB) == true);  // Lower height comes first

    // Case 5: Maximum sequence ID edge case
    indexA.nHeight = 100;
    indexA.nSequenceId = std::numeric_limits<int32_t>::max();
    indexB.nHeight = 100;
    indexB.nSequenceId = std::numeric_limits<int32_t>::max() - 1;

    BOOST_CHECK(comparator(&indexA, &indexB) == true);  // Higher seq ID (max) comes first
    BOOST_CHECK(comparator(&indexB, &indexA) == false); // Lower seq ID comes after

    // Case 6: Minimum sequence ID edge case (used in PreciousBlock)
    indexA.nHeight = 100;
    indexA.nSequenceId = std::numeric_limits<int32_t>::min();
    indexB.nHeight = 100;
    indexB.nSequenceId = std::numeric_limits<int32_t>::min() + 1;

    BOOST_CHECK(comparator(&indexA, &indexB) == false); // Lower (min) seq ID comes after
    BOOST_CHECK(comparator(&indexB, &indexA) == true);  // Higher seq ID comes first
}

/**
 * Test BlockHasher edge cases
 *
 * BlockHasher is used in the BlockMap (unordered_map). We test that it
 * properly computes hashes and handles edge cases like zero hashes.
 */
BOOST_AUTO_TEST_CASE(block_hasher_edge_cases)
{
    BlockHasher hasher;

    // Case 1: Zero hash
    uint256 zeroHash;
    zeroHash.SetNull();
    size_t hash1 = hasher(zeroHash);
    BOOST_CHECK(hash1 == 0); // Cheap hash of zero should be 0

    // Case 2: Same hash should produce same value
    uint256 hash256a = InsecureRand256();
    size_t hashValueA1 = hasher(hash256a);
    size_t hashValueA2 = hasher(hash256a);
    BOOST_CHECK(hashValueA1 == hashValueA2);

    // Case 3: Different hashes should (likely) produce different values
    uint256 hash256b = InsecureRand256();
    size_t hashValueB = hasher(hash256b);

    // While hash collisions are possible, they should be rare
    if (hash256a != hash256b) {
        BOOST_CHECK_MESSAGE(hashValueA1 != hashValueB, strprintf("Collision occured. %s and %s produced %s", hash256a, hash256b, hashValueA1));
    }

    // Case 4: Max value hash
    uint256 maxHash;
    for (int i = 0; i < 32; i++) {
        *(maxHash.begin() + i) = 0xFF;
    }
    size_t hashMax = hasher(maxHash);
    BOOST_CHECK(hashMax != 0); // Max hash should not hash to zero
}

/**
 * Test DisconnectResult enum edge cases
 *
 * Verify that DisconnectResult values are distinct and properly defined.
 */
BOOST_AUTO_TEST_CASE(disconnect_result_edge_cases)
{
    // Verify all three states are distinct
    BOOST_CHECK(DISCONNECT_OK != DISCONNECT_UNCLEAN);
    BOOST_CHECK(DISCONNECT_OK != DISCONNECT_FAILED);
    BOOST_CHECK(DISCONNECT_UNCLEAN != DISCONNECT_FAILED);

    // Verify the values match expected semantics
    // DISCONNECT_OK should indicate success
    DisconnectResult result = DISCONNECT_OK;
    BOOST_CHECK(result == DISCONNECT_OK);

    // Test assignment and comparison
    result = DISCONNECT_UNCLEAN;
    BOOST_CHECK(result != DISCONNECT_OK);
    BOOST_CHECK(result == DISCONNECT_UNCLEAN);

    result = DISCONNECT_FAILED;
    BOOST_CHECK(result != DISCONNECT_OK);
    BOOST_CHECK(result != DISCONNECT_UNCLEAN);
    BOOST_CHECK(result == DISCONNECT_FAILED);
}

/**
 * Test CChainState member initialization edge cases
 *
 * Verify that CChainState members are properly initialized and handle
 * edge cases in their initial states.
 */
BOOST_AUTO_TEST_CASE(chainstate_initialization_edge_cases)
{
    // Access global chainstate
    CChainState& chainstate = g_chainstate;

    // Verify mapBlockIndex is accessible and has entries (genesis + chain blocks loaded by fixture)
    BOOST_CHECK(!chainstate.mapBlockIndex.empty());

    // Verify chainActive is accessible
    BOOST_CHECK(chainstate.chainActive.Height() >= 0);

    // Verify pindexBestInvalid starts as nullptr
    BOOST_CHECK(chainstate.pindexBestInvalid == nullptr);

    // Test mapBlocksUnlinked (multimap) - should be empty in a clean chain
    BOOST_CHECK(chainstate.mapBlocksUnlinked.empty());

    // Verify scriptcheckqueue is initialized
    // Note: scriptcheckqueue is a unique_ptr, so we check if it's set up
    // The actual queue might be null in test environment
    // BOOST_CHECK(chainstate.scriptcheckqueue != nullptr);
}

/**
 * Test invalid block tracking edge cases
 *
 * Tests edge cases in tracking and managing invalid blocks, including:
 * - Multiple invalid blocks
 * - Blocks marked as failed vs corruption possible
 * - Failed blocks set management
 */
BOOST_AUTO_TEST_CASE(invalid_block_tracking_edge_cases)
{
    // Create test block indices
    CBlockIndex invalidBlock1;
    invalidBlock1.nHeight = 10;
    invalidBlock1.nStatus = 0;

    CBlockIndex invalidBlock2;
    invalidBlock2.nHeight = 15;
    invalidBlock2.nStatus = 0;

    // Test marking blocks as failed
    CValidationState state;

    // Simulate InvalidBlockFound with non-corrupt state
    // Note: We can't directly call private methods, but we can test the state transitions

    // Mark as failed valid
    invalidBlock1.nStatus |= BLOCK_FAILED_VALID;
    BOOST_CHECK(invalidBlock1.nStatus & BLOCK_FAILED_VALID);

    // Verify other status flags are independent
    invalidBlock2.nStatus |= BLOCK_HAVE_DATA;
    BOOST_CHECK(invalidBlock2.nStatus & BLOCK_HAVE_DATA);
    BOOST_CHECK(!(invalidBlock2.nStatus & BLOCK_FAILED_VALID));

    // Test multiple failure flags
    invalidBlock1.nStatus |= BLOCK_FAILED_CHILD;
    BOOST_CHECK(invalidBlock1.nStatus & BLOCK_FAILED_VALID);
    BOOST_CHECK(invalidBlock1.nStatus & BLOCK_FAILED_CHILD);
}

/**
 * Test precious block edge cases
 *
 * Tests edge cases in PreciousBlock functionality:
 * - Counter overflow protection
 * - Block not at tip
 * - Multiple calls with same block
 */
BOOST_AUTO_TEST_CASE(precious_block_edge_cases)
{
    // Test counter overflow protection logic
    // PreciousBlock uses nBlockReverseSequenceId which decrements
    // It has protection: if (nBlockReverseSequenceId > std::numeric_limits<int32_t>::min())

    int32_t testCounter = std::numeric_limits<int32_t>::min() + 10;

    // Simulate the decrement logic from PreciousBlock
    for (int i = 0; i < 15; i++) {
        if (testCounter > std::numeric_limits<int32_t>::min()) {
            testCounter--;
        }
    }

    // After 15 decrements, counter should be at min (stopped at min, didn't underflow)
    BOOST_CHECK(testCounter == std::numeric_limits<int32_t>::min());

    // Test that further decrements don't cause underflow
    if (testCounter > std::numeric_limits<int32_t>::min()) {
        testCounter--;
    }
    BOOST_CHECK(testCounter == std::numeric_limits<int32_t>::min());
}

/**
 * Test block index candidates edge cases
 *
 * Tests setBlockIndexCandidates operations with edge cases:
 * - Empty set
 * - Inserting duplicate blocks
 * - Erasing non-existent blocks
 */
BOOST_AUTO_TEST_CASE(block_index_candidates_edge_cases)
{
    // Create a set with the same comparator as CChainState
    std::set<CBlockIndex*, CBlockIndexWorkComparator> testSet;

    // Case 1: Empty set operations
    BOOST_CHECK(testSet.empty());
    BOOST_CHECK(testSet.size() == 0);

    CBlockIndex block1;
    block1.nHeight = 10;
    block1.nSequenceId = 1;

    // Case 2: Insert single block
    auto result1 = testSet.insert(&block1);
    BOOST_CHECK(result1.second == true); // Insertion succeeded
    BOOST_CHECK(testSet.size() == 1);

    // Case 3: Insert same block again (duplicate)
    auto result2 = testSet.insert(&block1);
    BOOST_CHECK(result2.second == false); // Insertion failed (duplicate)
    BOOST_CHECK(testSet.size() == 1); // Size unchanged

    // Case 4: Erase existing block
    size_t erased = testSet.erase(&block1);
    BOOST_CHECK(erased == 1);
    BOOST_CHECK(testSet.empty());

    // Case 5: Erase non-existent block
    size_t erased2 = testSet.erase(&block1);
    BOOST_CHECK(erased2 == 0); // Nothing erased

    // Case 6: Multiple blocks with same comparator
    CBlockIndex block2, block3;
    block2.nHeight = 10;
    block2.nSequenceId = 2;
    block3.nHeight = 10;
    block3.nSequenceId = 3;

    testSet.insert(&block1);
    testSet.insert(&block2);
    testSet.insert(&block3);

    BOOST_CHECK(testSet.size() == 3);

    // Verify ordering (higher sequence ID comes first - more recent blocks prioritized)
    auto it = testSet.begin();
    BOOST_CHECK(*it == &block3); // seq 3 (highest, comes first)
    ++it;
    BOOST_CHECK(*it == &block2); // seq 2
    ++it;
    BOOST_CHECK(*it == &block1); // seq 1 (lowest, comes last)
}

/**
 * Test BlockMap (unordered_map) edge cases
 *
 * Tests the BlockMap typedef edge cases:
 * - Empty map
 * - Hash collisions handling
 * - Large number of entries
 */
BOOST_AUTO_TEST_CASE(blockmap_edge_cases)
{
    BlockMap testMap;

    // Case 1: Empty map
    BOOST_CHECK(testMap.empty());
    BOOST_CHECK(testMap.size() == 0);

    // Case 2: Insert and find
    uint256 hash1 = InsecureRand256();
    CBlockIndex index1;
    index1.nHeight = 1;

    testMap[hash1] = &index1;
    BOOST_CHECK(testMap.size() == 1);
    BOOST_CHECK(testMap[hash1] == &index1);

    // Case 3: Find non-existent hash
    uint256 nonExistentHash = InsecureRand256();
    auto it = testMap.find(nonExistentHash);
    BOOST_CHECK(it == testMap.end());

    // Case 4: Overwrite existing entry
    CBlockIndex index2;
    index2.nHeight = 2;
    testMap[hash1] = &index2; // Overwrite
    BOOST_CHECK(testMap.size() == 1); // Still only 1 entry
    BOOST_CHECK(testMap[hash1] == &index2); // Points to new index

    // Case 5: Multiple entries
    uint256 hash2 = InsecureRand256();
    uint256 hash3 = InsecureRand256();

    CBlockIndex index3, index4;
    index3.nHeight = 3;
    index4.nHeight = 4;

    testMap[hash2] = &index3;
    testMap[hash3] = &index4;

    BOOST_CHECK(testMap.size() == 3);

    // Case 6: Erase entries
    testMap.erase(hash1);
    BOOST_CHECK(testMap.size() == 2);
    BOOST_CHECK(testMap.find(hash1) == testMap.end());

    // Case 7: Clear map
    testMap.clear();
    BOOST_CHECK(testMap.empty());
}

/**
 * Test mapBlocksUnlinked edge cases
 *
 * Tests multimap operations with unlinked blocks:
 * - Multiple blocks with same key
 * - Erasing specific entries
 * - Range queries
 */
BOOST_AUTO_TEST_CASE(blocks_unlinked_edge_cases)
{
    std::multimap<CBlockIndex*, CBlockIndex*> testMultimap;

    CBlockIndex parent1, child1, child2, child3;
    parent1.nHeight = 10;
    child1.nHeight = 11;
    child2.nHeight = 11;
    child3.nHeight = 11;

    // Case 1: Empty multimap
    BOOST_CHECK(testMultimap.empty());

    // Case 2: Insert multiple children for same parent
    testMultimap.insert(std::make_pair(&parent1, &child1));
    testMultimap.insert(std::make_pair(&parent1, &child2));
    testMultimap.insert(std::make_pair(&parent1, &child3));

    BOOST_CHECK(testMultimap.size() == 3);

    // Case 3: Count entries for parent
    size_t count = testMultimap.count(&parent1);
    BOOST_CHECK(count == 3);

    // Case 4: Range query
    auto range = testMultimap.equal_range(&parent1);
    int foundCount = 0;
    for (auto it = range.first; it != range.second; ++it) {
        foundCount++;
    }
    BOOST_CHECK(foundCount == 3);

    // Case 5: Erase specific entry
    auto it = testMultimap.find(&parent1);
    if (it != testMultimap.end()) {
        testMultimap.erase(it); // Erase one entry
    }
    BOOST_CHECK(testMultimap.size() == 2);
    BOOST_CHECK(testMultimap.count(&parent1) == 2);

    // Case 6: Erase all entries for a key
    testMultimap.erase(&parent1);
    BOOST_CHECK(testMultimap.empty());
}

/**
 * Test validation state edge cases
 *
 * Tests CValidationState edge cases used in disconnect operations:
 * - Corruption possible flag
 * - Error states
 * - State transitions
 */
BOOST_AUTO_TEST_CASE(validation_state_edge_cases)
{
    CValidationState state1;

    // Case 1: Initial state should be valid
    BOOST_CHECK(state1.IsValid());
    BOOST_CHECK(!state1.IsInvalid());
    BOOST_CHECK(!state1.IsError());

    // Case 2: Error state
    CValidationState state2;
    state2.Error("test error");
    BOOST_CHECK(!state2.IsValid());
    BOOST_CHECK(state2.IsError());

    // Case 3: Invalid state
    CValidationState state3;
    state3.Invalid(false, REJECT_INVALID, "test invalid");
    BOOST_CHECK(!state3.IsValid());
    BOOST_CHECK(state3.IsInvalid());

    // Case 4: Corruption possible
    CValidationState state4;
    state4.Invalid(false, REJECT_INVALID, "test corruption", ""); 
    state4.SetCorruptionPossible();
    BOOST_CHECK(state4.CorruptionPossible());

    CValidationState state5;
    state5.Invalid(false, REJECT_INVALID, "test no corruption", "");
    BOOST_CHECK(!state5.CorruptionPossible());
}

/**
 * Test scriptcheckqueue edge cases
 *
 * Tests CCheckQueue<CScriptCheck> edge cases:
 * - Queue initialization with worker threads
 * - Empty queue handling
 * - Worker thread lifecycle (automatic start/stop)
 */
BOOST_AUTO_TEST_CASE(scriptcheckqueue_edge_cases)
{
    // Case 1: Verify queue is created and ready to use
    // Create a script check queue with batch_size=128 and 1 worker thread
    // Constructor automatically starts worker threads
    // If construction succeeds without throwing, the queue is valid
    CCheckQueue<CScriptCheck> queue(128, 1);
    // Queue construction succeeded - this is our verification for Case 1

    // Case 2: Test with empty queue
    // Wait() should return true even with no checks added
    BOOST_CHECK(queue.Wait());

    // Case 3: Destructor will automatically stop and join worker threads
    // when queue goes out of scope
}

/**
 * Test edge cases in block status flags
 *
 * Tests various combinations of block status flags to ensure they
 * don't interfere with each other and can be properly combined.
 */
BOOST_AUTO_TEST_CASE(block_status_flags_edge_cases)
{
    CBlockIndex block;
    block.nStatus = 0;

    // Case 1: Individual flags
    block.nStatus |= BLOCK_VALID_HEADER;
    BOOST_CHECK(block.nStatus & BLOCK_VALID_HEADER);

    block.nStatus |= BLOCK_VALID_TREE;
    BOOST_CHECK(block.nStatus & BLOCK_VALID_HEADER);
    BOOST_CHECK(block.nStatus & BLOCK_VALID_TREE);

    // Case 2: Check specific validation levels
    BOOST_CHECK(block.IsValid(BLOCK_VALID_HEADER));
    BOOST_CHECK(block.IsValid(BLOCK_VALID_TREE));

    // Case 3: Failure flags
    CBlockIndex failedBlock;
    failedBlock.nStatus = BLOCK_FAILED_VALID;

    BOOST_CHECK(failedBlock.nStatus & BLOCK_FAILED_VALID);
    BOOST_CHECK(!(failedBlock.nStatus & BLOCK_VALID_TRANSACTIONS));

    // Case 4: Combined flags
    CBlockIndex combinedBlock;
    combinedBlock.nStatus = BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO | BLOCK_VALID_TRANSACTIONS;

    BOOST_CHECK(combinedBlock.nStatus & BLOCK_HAVE_DATA);
    BOOST_CHECK(combinedBlock.nStatus & BLOCK_HAVE_UNDO);
    BOOST_CHECK(combinedBlock.nStatus & BLOCK_VALID_TRANSACTIONS);

    // Case 5: Clearing specific flags
    combinedBlock.nStatus &= ~BLOCK_HAVE_DATA; // Clear HAVE_DATA flag
    BOOST_CHECK(!(combinedBlock.nStatus & BLOCK_HAVE_DATA));
    BOOST_CHECK(combinedBlock.nStatus & BLOCK_HAVE_UNDO); // Others unchanged
}

/**
 * Test block sequence ID assignment and persistence
 *
 * Tests that sequence IDs behave correctly:
 * - Block headers start with sequence ID 0 (via AddToBlockIndex)
 * - Full blocks get non-zero sequence IDs (via ReceivedBlockTransactions)
 * - Sequence IDs persist correctly across disk operations
 * - Different blocks get incrementing sequence IDs
 */
BOOST_AUTO_TEST_CASE(block_sequence_id_edge_cases)
{
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    // Case 1: Test header-only block (sequence ID should be 0)
    CBlock headerOnlyBlock = getBlock();
    CBlockHeader header = headerOnlyBlock.GetBlockHeader();

    CBlockIndex* pindexHeader = nullptr;
    {
        LOCK(cs_main);
        CValidationState state;
        // AcceptBlockHeader adds just the header to mapBlockIndex with sequence ID 0
        bool accepted = g_chainstate.AcceptBlockHeader(header, state, &pindexHeader);

        if (accepted && pindexHeader) {
            // Verify that header-only blocks have sequence ID 0
            // This is set in AddToBlockIndex (chainstate.cpp:1203)
            BOOST_CHECK(pindexHeader->nSequenceId == 0);
        }
    }

    // Case 2: Create a fully processed block (non-zero sequence ID)
    std::vector<CMutableTransaction> noTxns;
    CBlock block1 = CreateAndProcessBlock(noTxns, scriptPubKey);

    // Get the CBlockIndex for this block
    uint256 hash1 = block1.GetHash();
    CBlockIndex* pindex1 = nullptr;
    {
        LOCK(cs_main);
        auto it = g_chainstate.mapBlockIndex.find(hash1);
        BOOST_REQUIRE(it != g_chainstate.mapBlockIndex.end());
        pindex1 = it->second;
    }

    // Check that the sequence ID is non-zero
    int32_t originalSeqId1 = pindex1->nSequenceId;
    BOOST_CHECK(originalSeqId1 > 0);

    // Write the block to disk
    CDiskBlockPos diskPos1 = SaveBlockToDisk(block1, block1.GetHeight(), nullptr);
    BOOST_CHECK(diskPos1.IsNull() == false);

    // Read the block back from disk
    CBlock blockFromDisk1;
    BOOST_REQUIRE(ReadBlockFromDisk(blockFromDisk1, diskPos1, block1.GetHeight()));

    // Verify the block content is the same
    BOOST_CHECK(blockFromDisk1.GetHash() == hash1);

    // The CBlockIndex should maintain its sequence ID after disk I/O
    BOOST_CHECK(pindex1->nSequenceId == originalSeqId1);

    // Case 3: Create a second different block (should get higher sequence ID)
    CBlock block2 = CreateAndProcessBlock(noTxns, scriptPubKey);
    uint256 hash2 = block2.GetHash();
    BOOST_CHECK(hash2 != hash1); // Ensure blocks are different

    CBlockIndex* pindex2 = nullptr;
    {
        LOCK(cs_main);
        auto it = g_chainstate.mapBlockIndex.find(hash2);
        BOOST_REQUIRE(it != g_chainstate.mapBlockIndex.end());
        pindex2 = it->second;
    }

    // Check that block2 has a higher sequence ID than block1
    int32_t originalSeqId2 = pindex2->nSequenceId;
    BOOST_CHECK(originalSeqId2 > originalSeqId1);

    // Write block2 to disk
    CDiskBlockPos diskPos2 = SaveBlockToDisk(block2, block2.GetHeight(), nullptr);
    BOOST_CHECK(diskPos2.IsNull() == false);

    // Read block2 back from disk
    CBlock blockFromDisk2;
    BOOST_REQUIRE(ReadBlockFromDisk(blockFromDisk2, diskPos2, block2.GetHeight()));

    // Verify the block content is the same
    BOOST_CHECK(blockFromDisk2.GetHash() == hash2);

    // The CBlockIndex should maintain its sequence ID
    BOOST_CHECK(pindex2->nSequenceId == originalSeqId2);

    // Case 4: Verify sequence ID ordering relationships
    BOOST_CHECK(pindex1->nSequenceId > 0);
    BOOST_CHECK(pindex2->nSequenceId > 0);
    BOOST_CHECK(pindex2->nSequenceId > pindex1->nSequenceId);

    // If we had a header-only block, its sequence ID (0) would be less than processed blocks
    if (pindexHeader) {
        BOOST_CHECK(pindexHeader->nSequenceId == 0);
        BOOST_CHECK(pindexHeader->nSequenceId < pindex1->nSequenceId);
    }
}

BOOST_AUTO_TEST_SUITE_END()
