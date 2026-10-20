#include <gtest/gtest.h>

#include <bfc/event_queue.hpp>

#include <memory>
#include <utility>

using namespace bfc;

namespace
{

bool push_overran(reactive_circular_lossy_queue<int>& q, size_t size_before_push)
{
    return q.capacity() > 0 && size_before_push == q.capacity();
}

struct non_default_constructible
{
    explicit non_default_constructible(int v)
        : value(v)
    {}

    non_default_constructible(non_default_constructible&&) = default;
    non_default_constructible& operator=(non_default_constructible&&) = default;

    int value;
};

} // namespace

TEST(reactive_circular_lossy_queue, fifo_within_capacity)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(4);

    EXPECT_EQ(q.push(1), 1u);
    EXPECT_EQ(q.push(2), 2u);
    EXPECT_EQ(q.size(), 2u);

    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 1);
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 2);
    EXPECT_EQ(q.size(), 0u);
}

TEST(reactive_circular_lossy_queue, drops_oldest_when_full)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);

    EXPECT_EQ(q.push(1), 1u);
    EXPECT_EQ(q.push(2), 2u);
    EXPECT_EQ(q.push(3), 2u);

    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 2);
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 3);
    EXPECT_EQ(q.size(), 0u);
}

TEST(reactive_circular_lossy_queue, set_capacity_shrink_drops_oldest)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(4);
    q.push(1);
    q.push(2);
    q.push(3);

    q.set_capacity(2);
    EXPECT_EQ(q.size(), 2u);
    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 2);
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 3);
}

TEST(reactive_circular_lossy_queue, set_capacity_grow_preserves_order)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);
    q.push(1);
    q.push(2);

    q.set_capacity(4);
    EXPECT_EQ(q.size(), 2u);
    q.push(3);

    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 1);
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 2);
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 3);
}

TEST(reactive_circular_lossy_queue, pop_empty_returns_false)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(1);
    int v = 0;
    EXPECT_FALSE(q.pop(v));
}

TEST(reactive_circular_lossy_queue, push_without_capacity_returns_zero)
{
    reactive_circular_lossy_queue<int> q;
    EXPECT_EQ(q.push(1), 0u);
    EXPECT_EQ(q.size(), 0u);
}

TEST(reactive_circular_lossy_queue, overrun_detected_from_size_before_push)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);

    size_t before = q.size();
    EXPECT_EQ(q.push(1), 1u);
    EXPECT_FALSE(push_overran(q, before));

    before = q.size();
    EXPECT_EQ(q.push(2), 2u);
    EXPECT_FALSE(push_overran(q, before));

    before = q.size();
    EXPECT_EQ(q.push(3), 2u);
    EXPECT_TRUE(push_overran(q, before));

    before = q.size();
    EXPECT_EQ(q.push(4), 2u);
    EXPECT_TRUE(push_overran(q, before));

    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 3);
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 4);
}

TEST(reactive_circular_lossy_queue, push_returns_size_after_push_when_full)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);
    q.push(1);
    q.push(2);
    EXPECT_EQ(q.push(99), 2u);
    EXPECT_EQ(q.size(), 2u);
}

TEST(reactive_circular_lossy_queue, non_default_constructible_element)
{
    reactive_circular_lossy_queue<non_default_constructible> q;
    q.set_capacity(2);
    EXPECT_EQ(q.push(non_default_constructible{10}), 1u);
    EXPECT_EQ(q.push(non_default_constructible{20}), 2u);

    non_default_constructible out{0};
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(out.value, 10);
}

TEST(reactive_circular_lossy_queue, move_only_element)
{
    reactive_circular_lossy_queue<std::unique_ptr<int>> q;
    q.set_capacity(1);
    EXPECT_EQ(q.push(std::make_unique<int>(7)), 1u);

    std::unique_ptr<int> out;
    EXPECT_TRUE(q.pop(out));
    ASSERT_TRUE(out);
    EXPECT_EQ(*out, 7);
}

TEST(reactive_circular_lossy_queue, has_data_reflects_occupancy)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);
    EXPECT_FALSE(q.has_data());
    q.push(1);
    EXPECT_TRUE(q.has_data());
    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_FALSE(q.has_data());
}

TEST(reactive_circular_lossy_queue, notify_callback_invokes_handler)
{
    reactive_circular_lossy_queue<int> q;
    int calls = 0;
    q.set_callback([&calls]() { ++calls; });
    q.notify_callback();
    EXPECT_EQ(calls, 1);
    q.notify_callback();
    EXPECT_EQ(calls, 2);
}

TEST(reactive_circular_lossy_queue, cv_reactor_registry_alias)
{
    auto queue = std::make_shared<cv_reactor_circular_lossy_queue_tag<>::type_t>();
    auto& registry = cv_reactor_circular_lossy_queue_registry<>::get_instance();
    EXPECT_TRUE(registry.add("lossy-events", queue));
    EXPECT_EQ(registry.get("lossy-events"), queue);
    EXPECT_TRUE(registry.rem("lossy-events"));
}

TEST(reactive_circular_lossy_queue, ring_indices_wrap_across_capacity)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(3);
    for (int i = 1; i <= 6; ++i)
    {
        q.push(std::move(i));
    }
    EXPECT_EQ(q.size(), 3u);

    int v = 0;
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 4);
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 5);
    EXPECT_TRUE(q.pop(v));
    EXPECT_EQ(v, 6);
}

TEST(reactive_circular_lossy_queue, set_capacity_zero_clears_queue)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);
    q.push(1);
    q.push(2);
    q.set_capacity(0);
    EXPECT_EQ(q.capacity(), 0u);
    EXPECT_EQ(q.size(), 0u);
    EXPECT_FALSE(q.has_data());
    EXPECT_EQ(q.push(3), 0u);
}
