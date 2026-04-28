// Copyright (c) 2022-present The Bitcoin Core developers
// Copyright (c) 2019-2024 Chaintope Inc.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Ported from Bitcoin Core src/test/pool_tests.cpp (PR #25325).
// Adaptations for Tapyrus:
//   - BasicTestingSetup and random helpers from test/test_tapyrus.h
//   - InsecureRandRange() / InsecureRandBool() replace m_rng.randrange()
//   - ASAN poison/unpoison calls removed (not used in Tapyrus CI)
//   - std::span available (C++20 is required by CMakeLists.txt)

// These must precede memusage.h: Tapyrus's memusage.h includes indirectmap.h
// (which needs std::map) and references prevector<> without including either.
#include <map>
#include <prevector.h>
#include <memusage.h>
#include <support/allocators/pool.h>
#include <test/poolresourcetester.h>
#include <test/test_tapyrus.h>

#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(pool_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(basic_allocating)
{
    auto resource = PoolResource<8, 8>();
    PoolResourceTester::CheckAllDataAccountedFor(resource);

    // First chunk is already allocated.
    size_t expected_bytes_available = resource.ChunkSizeBytes();
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    // Consume one block from the chunk.
    void* block = resource.Allocate(8, 8);
    expected_bytes_available -= 8;
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    BOOST_TEST(0 == PoolResourceTester::FreeListSizes(resource)[1]);
    resource.Deallocate(block, 8, 8);
    PoolResourceTester::CheckAllDataAccountedFor(resource);
    BOOST_TEST(1 == PoolResourceTester::FreeListSizes(resource)[1]);

    // Alignment smaller than block size: the best-fitting freelist is used; nothing new allocated.
    void* b = resource.Allocate(8, 1);
    BOOST_TEST(b == block); // same block reused
    BOOST_TEST(0 == PoolResourceTester::FreeListSizes(resource)[1]);
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    resource.Deallocate(block, 8, 1);
    PoolResourceTester::CheckAllDataAccountedFor(resource);
    BOOST_TEST(1 == PoolResourceTester::FreeListSizes(resource)[1]);
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    // Alignment too large for pool: falls back to system allocator.
    b = resource.Allocate(8, 16);
    BOOST_TEST(b != block);
    block = b;
    PoolResourceTester::CheckAllDataAccountedFor(resource);
    BOOST_TEST(1 == PoolResourceTester::FreeListSizes(resource)[1]);
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    resource.Deallocate(block, 8, 16);
    PoolResourceTester::CheckAllDataAccountedFor(resource);
    BOOST_TEST(1 == PoolResourceTester::FreeListSizes(resource)[1]);
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    // Size too large for pool: falls back to system allocator.
    block = resource.Allocate(16, 8);
    PoolResourceTester::CheckAllDataAccountedFor(resource);
    BOOST_TEST(1 == PoolResourceTester::FreeListSizes(resource)[1]);
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    resource.Deallocate(block, 16, 8);
    PoolResourceTester::CheckAllDataAccountedFor(resource);
    BOOST_TEST(1 == PoolResourceTester::FreeListSizes(resource)[1]);
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    // Zero-byte allocation: forwarded to operator new; consumes one freelist entry.
    void* p = resource.Allocate(0, 1);
    BOOST_TEST(0 == PoolResourceTester::FreeListSizes(resource)[1]);
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));

    resource.Deallocate(p, 0, 1);
    PoolResourceTester::CheckAllDataAccountedFor(resource);
    BOOST_TEST(1 == PoolResourceTester::FreeListSizes(resource)[1]);
    BOOST_TEST(expected_bytes_available == PoolResourceTester::AvailableMemoryFromChunk(resource));
}

// Allocate 0..n bytes where n exceeds the pool's chunk size — every size must succeed.
BOOST_AUTO_TEST_CASE(allocate_any_byte)
{
    auto resource = PoolResource<128, 8>(1024);

    uint8_t num_allocs = 200;
    auto data = std::vector<std::span<uint8_t>>();

    for (uint8_t num_bytes = 0; num_bytes < num_allocs; ++num_bytes) {
        uint8_t* bytes = new (resource.Allocate(num_bytes, 1)) uint8_t[num_bytes];
        BOOST_TEST(bytes != nullptr);
        data.emplace_back(bytes, num_bytes);
        std::fill(bytes, bytes + num_bytes, num_bytes);
    }

    uint8_t val = 0;
    for (auto const& span : data) {
        for (auto x : span) {
            BOOST_TEST(val == x);
        }
        std::destroy(span.data(), span.data() + span.size());
        resource.Deallocate(span.data(), span.size(), 1);
        ++val;
    }

    PoolResourceTester::CheckAllDataAccountedFor(resource);
}

BOOST_AUTO_TEST_CASE(random_allocations)
{
    struct PtrSizeAlignment {
        void* ptr;
        size_t bytes;
        size_t alignment;
    };

    auto resource = PoolResource<128, 8>(65536);
    std::vector<PtrSizeAlignment> ptr_size_alignment{};

    for (size_t i = 0; i < 1000; ++i) {
        if (ptr_size_alignment.empty() || 0 != InsecureRandRange(4)) {
            std::size_t alignment = std::size_t{1} << InsecureRandRange(8);          // 1, 2, ..., 128
            std::size_t size = (InsecureRandRange(200) / alignment + 1) * alignment; // multiple of alignment
            void* ptr = resource.Allocate(size, alignment);
            BOOST_TEST(ptr != nullptr);
            BOOST_TEST((reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0);
            ptr_size_alignment.push_back({ptr, size, alignment});
        } else {
            auto& x = ptr_size_alignment[InsecureRandRange(ptr_size_alignment.size())];
            resource.Deallocate(x.ptr, x.bytes, x.alignment);
            x = ptr_size_alignment.back();
            ptr_size_alignment.pop_back();
        }
    }

    for (auto const& x : ptr_size_alignment) {
        resource.Deallocate(x.ptr, x.bytes, x.alignment);
    }

    PoolResourceTester::CheckAllDataAccountedFor(resource);
}

BOOST_AUTO_TEST_CASE(memusage_test)
{
    auto std_map = std::unordered_map<int64_t, int64_t>{};

    using Map = std::unordered_map<int64_t,
                                   int64_t,
                                   std::hash<int64_t>,
                                   std::equal_to<int64_t>,
                                   PoolAllocator<std::pair<const int64_t, int64_t>,
                                                 sizeof(std::pair<const int64_t, int64_t>) + sizeof(void*) * 4,
                                                 alignof(void*)>>;
    auto resource = Map::allocator_type::ResourceType(1024);

    PoolResourceTester::CheckAllDataAccountedFor(resource);

    {
        auto resource_map = Map{0, std::hash<int64_t>{}, std::equal_to<int64_t>{}, &resource};

        BOOST_TEST(memusage::DynamicUsage(std_map) != memusage::DynamicUsage(resource_map));

        for (size_t i = 0; i < 10000; ++i) {
            std_map[i];
            resource_map[i];
        }

        // Pool allocator has significantly lower overhead than the system allocator.
        BOOST_TEST(memusage::DynamicUsage(resource_map) <= memusage::DynamicUsage(std_map) * 90 / 100);

        auto max_nodes_per_chunk = resource.ChunkSizeBytes() / sizeof(Map::value_type);
        auto min_num_allocated_chunks = resource_map.size() / max_nodes_per_chunk + 1;
        BOOST_TEST(resource.NumAllocatedChunks() >= min_num_allocated_chunks);
    }

    PoolResourceTester::CheckAllDataAccountedFor(resource);
}

BOOST_AUTO_TEST_SUITE_END()
