// Copyright (c) 2025 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <federationparams.h>
#include <primitives/block.h>
#include <validation.h>
#include <file_io.h>
#include <test/test_tapyrus.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(file_io_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(file_io_find_block_pos)
{
    // Test that SaveBlockToDisk correctly handles the serialization header size
    // during normal operation and during reindex scenarios.

    // This test verifies the fix for incorrect blk file size calculation during reindex
    // that results in recoverable blk file corruption (issue #21379 in Bitcoin).

    // The bug was that during reindex, when blocks are read from disk and saved back,
    // the file position tracking was incorrect because the 8-byte serialization header
    // (4 bytes magic + 4 bytes size) was not properly accounted for.

    const CBlock& genesisBlock = FederationParams().GenesisBlock();

    // Scenario 1: Normal block addition (first block written to disk)
    // When a genesis block is added normally, it should be written at offset 8
    // (after the 8-byte serialization header)
    CDiskBlockPos pos1 = SaveBlockToDisk(genesisBlock, 0, nullptr);
    BOOST_CHECK_EQUAL(pos1.nPos, BLOCK_SERIALIZATION_HEADER_SIZE);

    // Scenario 2: Simulate what happens during reindex
    // During reindex, blocks are found at known positions in the blk file.
    // The genesis block is found at offset 8 (after its serialization header).
    CDiskBlockPos knownPos(0, BLOCK_SERIALIZATION_HEADER_SIZE);
    CDiskBlockPos pos2 = SaveBlockToDisk(genesisBlock, 0, &knownPos);
    BOOST_CHECK_EQUAL(pos2.nPos, BLOCK_SERIALIZATION_HEADER_SIZE);

    // Scenario 3: After reindex, when a new block is processed
    // This is the critical test that verifies the fix.
    // The new block should be written at the correct position:
    // 8 bytes (serialization header)
    // + size of genesis block
    // + 8 bytes (serialization header for the new block)
    CDiskBlockPos pos3 = SaveBlockToDisk(genesisBlock, 1, nullptr);
    unsigned int expectedPos = BLOCK_SERIALIZATION_HEADER_SIZE +
                               ::GetSerializeSize(genesisBlock, SER_DISK, CLIENT_VERSION) +
                               BLOCK_SERIALIZATION_HEADER_SIZE;
    BOOST_CHECK_EQUAL(pos3.nPos, expectedPos);

    // This assertion ensures that the bug described in issue #21379 does not recur:
    // Before the fix, the code was adding the 8-byte header in FindBlockPos but not
    // accounting for it correctly in the block size calculation, leading to incorrect
    // file positions and data gaps after reindex.
    //
    // The test verifies that after a reindex (simulated by passing a known position),
    // the next new block is written at the correct position, accounting for both the
    // previous block data and all serialization headers.
}

BOOST_AUTO_TEST_SUITE_END()
