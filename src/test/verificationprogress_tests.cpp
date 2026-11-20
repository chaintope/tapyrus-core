// Copyright (c) 2026 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockindex.h>
#include <sync.h>
#include <test/test_tapyrus.h>
#include <verificationprogress.h>

#include <boost/test/unit_test.hpp>

#include <ctime>
#include <vector>

namespace {
//! A connected chain of heights 0 to tip, each block step_seconds after the
//! one before and holding txs_per_block transactions.
class TestChain
{
public:
    TestChain(int32_t tip, int64_t step_seconds, uint32_t txs_per_block, int64_t start_time) : m_blocks(tip + 1)
    {
        for (int32_t height = 0; height <= tip; ++height) {
            CBlockIndex& block = m_blocks[height];
            block.nHeight = height;
            block.nTime = static_cast<uint32_t>(start_time + height * step_seconds);
            block.nChainTx = static_cast<unsigned int>((height + 1) * txs_per_block);
            block.pprev = height > 0 ? &m_blocks[height - 1] : nullptr;
        }
    }

    const CBlockIndex* At(int32_t height) const { return &m_blocks[height]; }

private:
    std::vector<CBlockIndex> m_blocks;
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(verificationprogress_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(progress_is_the_share_of_the_expected_transactions)
{
    VerificationProgress progress;
    progress.Set({time(nullptr), 1000, 0.0});
    const TestChain chain{3, 600, 100, time(nullptr) - 3600};
    BOOST_CHECK_CLOSE(progress.Guess(chain.At(1)), 0.2, 0.001);
    BOOST_CHECK_CLOSE(progress.Guess(chain.At(3)), 0.4, 0.001);
}

BOOST_AUTO_TEST_CASE(a_block_past_the_estimate_is_complete_at_its_own_time)
{
    VerificationProgress progress;
    progress.Set({time(nullptr), 10, 0.0});
    const TestChain chain{3, 600, 100, time(nullptr) - 1800};
    BOOST_CHECK_CLOSE(progress.Guess(chain.At(3)), 1.0, 0.001);
}

BOOST_AUTO_TEST_CASE(a_refresh_on_a_window_boundary_measures_the_rate)
{
    // The window is 100 blocks below a height of 10000; at height 200 it spans
    // heights 100 to 200: 200 transactions in 1000 seconds.
    const TestChain chain{200, 10, 2, time(nullptr) - 5000};
    VerificationProgress progress;
    {
        LOCK(cs_main);
        progress.Refresh(chain.At(200), nullptr);
    }
    // 402 transactions are expected at the tip, so the tip is complete and height 100 about half way.
    BOOST_CHECK_CLOSE(progress.Guess(chain.At(200)), 1.0, 1.0);
    BOOST_CHECK_CLOSE(progress.Guess(chain.At(100)), 202.0 / 402.0, 1.0);
}

BOOST_AUTO_TEST_CASE(a_best_header_ahead_of_the_tip_raises_the_expected_total)
{
    const TestChain chain{200, 10, 2, time(nullptr) - 5000};
    const TestChain headers{400, 10, 2, time(nullptr) - 5000};
    VerificationProgress progress;
    {
        LOCK(cs_main);
        progress.Refresh(chain.At(200), headers.At(400));
    }
    // About 402 transactions per 200 blocks, so twice as many for 400 headers.
    BOOST_CHECK_CLOSE(progress.Guess(chain.At(200)), 0.5, 1.0);
}

// Negative cases: what must not count as progress or change the estimate.

BOOST_AUTO_TEST_CASE(a_null_or_unconnected_block_has_no_progress)
{
    VerificationProgress progress;
    progress.Set({time(nullptr), 1000, 0.0});
    BOOST_CHECK_EQUAL(progress.Guess(nullptr), 0.0);
    CBlockIndex header;
    header.nHeight = 5;
    BOOST_CHECK_EQUAL(progress.Guess(&header), 0.0);
}

BOOST_AUTO_TEST_CASE(a_refresh_off_the_window_boundary_changes_nothing)
{
    const TestChain chain{150, 10, 2, time(nullptr) - 5000};
    VerificationProgress progress;
    progress.Set({time(nullptr), 1000, 0.0});
    {
        LOCK(cs_main);
        progress.Refresh(chain.At(150), nullptr);
    }
    BOOST_CHECK_CLOSE(progress.Guess(chain.At(150)), 302.0 / 1000.0, 0.001);
}

BOOST_AUTO_TEST_CASE(a_window_without_elapsed_time_changes_nothing)
{
    const TestChain chain{200, 0, 2, time(nullptr) - 5000};
    VerificationProgress progress;
    progress.Set({time(nullptr), 1000, 0.0});
    {
        LOCK(cs_main);
        progress.Refresh(chain.At(200), nullptr);
        progress.Refresh(chain.At(0), nullptr);
    }
    BOOST_CHECK_CLOSE(progress.Guess(chain.At(200)), 402.0 / 1000.0, 0.001);
}

BOOST_AUTO_TEST_SUITE_END()
