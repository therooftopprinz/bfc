#include <gtest/gtest.h>

#include <bfc/config.hpp>

#include <cstdint>
#include <type_traits>

#include <bfc/event_queue.hpp>
#include <bfc/memory_pool.hpp>
#include <bfc/allocation.hpp>
#include <bfc/shared_sized_buffer.hpp>
#include <bfc/sized_buffer.hpp>


namespace
{
struct snapshot
{
#ifdef BFC_TEST_COUNTING_ALLOCATOR
    std::size_t a = bfc::config::allocations.load();
    std::size_t d = bfc::config::deallocations.load();
    std::size_t allocs() const { return bfc::config::allocations.load() - a; }
    std::size_t frees() const { return bfc::config::deallocations.load() - d; }
    static constexpr bool counting = true;
#else
    std::size_t allocs() const { return 0; }
    std::size_t frees() const { return 0; }
    static constexpr bool counting = false;
#endif
};
} // namespace

TEST(PayloadAllocTest, sized_buffer_uses_payload_allocator)
{
    snapshot s;
    {
        bfc::sized_buffer b;
        b.reserve(64);
        b.resize(10);
        if (snapshot::counting) EXPECT_EQ(1u, s.allocs());
        auto out = std::move(b).to_buffer();
        if (snapshot::counting) EXPECT_EQ(2u, s.allocs());
    }
    if (snapshot::counting) EXPECT_EQ(s.allocs(), s.frees());
}

TEST(PayloadAllocTest, shared_sized_buffer_storage_and_bytes_use_payload_allocator)
{
    snapshot s;
    {
        auto b = bfc::shared_sized_buffer::allocate(128);
        auto view = b.subview(0, 0);
        if (snapshot::counting) EXPECT_EQ(2u, s.allocs()); // control node + bytes
    }
    if (snapshot::counting) EXPECT_EQ(2u, s.allocs());
    if (snapshot::counting) EXPECT_EQ(2u, s.frees());
}

TEST(PayloadAllocTest, memory_pool_uses_payload_allocator)
{
    snapshot s;
    {
        bfc::log2_memory_pool<> pool;
        auto b = pool.allocate(100);
        if (snapshot::counting) EXPECT_EQ(1u, s.allocs());
    }
    if (snapshot::counting) EXPECT_EQ(s.allocs(), s.frees());
}

TEST(PayloadAllocTest, lossy_queue_slots_use_payload_allocator)
{
    snapshot s;
    {
        bfc::reactive_circular_lossy_queue<int> q;
        q.set_capacity(8);
        if (snapshot::counting) EXPECT_EQ(1u, s.allocs());
        q.set_capacity(16);
        if (snapshot::counting) EXPECT_EQ(2u, s.allocs());
    }
    if (snapshot::counting) EXPECT_EQ(s.allocs(), s.frees());
}

TEST(PayloadAllocTest, config_allocator_is_stateless_and_rebinds)
{
    static_assert(std::is_empty_v<bfc::config::allocator<int>>);
    bfc::config::allocator<int> a;
    bfc::config::allocator<double> b(a);
    EXPECT_TRUE(a == b);
}

TEST(PayloadAllocTest, aligned_bytes_are_aligned_and_balanced)
{
    snapshot s;
    auto* p = bfc::aligned_bytes_allocate<64>(100);
    EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(p) % 64);
    bfc::aligned_bytes_deallocate<64>(p, 100);
    if (snapshot::counting) EXPECT_EQ(1u, s.allocs());
    if (snapshot::counting) EXPECT_EQ(1u, s.frees());
}
