#include <gtest/gtest.h>

#include <bfc/cv_reactor.hpp>
#include <bfc/event_queue.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace bfc;

namespace
{

bool push_dropped(size_t size_before, size_t capacity)
{
    return capacity > 0 && size_before == capacity;
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

// Move-constructible only, so a full-slot push must overwrite via emplace.
struct move_construct_only
{
    explicit move_construct_only(int v) noexcept
        : value(v)
    {}

    move_construct_only(move_construct_only&& other) noexcept
        : value(other.value)
    {
        other.value = -1;
    }

    move_construct_only& operator=(move_construct_only&&) = delete;

    int value;
};

// ~T calls back into the queue. Drop and retire paths must release the queue
// mutex before that destructor runs.
struct reentrant_element
{
    reentrant_element(reactive_circular_lossy_queue<reentrant_element>* queue, int v) noexcept
        : queue(queue), value(v)
    {}

    reentrant_element(reentrant_element&& other) noexcept
        : queue(other.queue), value(other.value)
    {
        other.queue = nullptr;
    }

    reentrant_element& operator=(reentrant_element&&) = delete;

    ~reentrant_element()
    {
        if (queue != nullptr)
        {
            (void)queue->size();
        }
    }

    reactive_circular_lossy_queue<reentrant_element>* queue = nullptr;
    int value = 0;
};

bool wait_until(const std::atomic<bool>& flag,
    std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

} // namespace

TEST(reactive_circular_lossy_queue, fifo_within_capacity)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(4);

    EXPECT_EQ(q.push(1), 0u);
    EXPECT_EQ(q.push(2), 1u);
    EXPECT_EQ(q.size(), 2u);

    auto first = q.pop();
    auto second = q.pop();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, 1);
    EXPECT_EQ(*second, 2);
    EXPECT_FALSE(q.pop());
    EXPECT_EQ(q.size(), 0u);
}

TEST(reactive_circular_lossy_queue, drops_oldest_when_full)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);

    EXPECT_EQ(q.push(1), 0u);
    EXPECT_EQ(q.push(2), 1u);
    EXPECT_EQ(q.push(3), 2u);

    auto first = q.pop();
    auto second = q.pop();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, 2);
    EXPECT_EQ(*second, 3);
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
    auto first = q.pop();
    auto second = q.pop();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, 2);
    EXPECT_EQ(*second, 3);
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

    auto first = q.pop();
    auto second = q.pop();
    auto third = q.pop();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    EXPECT_EQ(*first, 1);
    EXPECT_EQ(*second, 2);
    EXPECT_EQ(*third, 3);
}

TEST(reactive_circular_lossy_queue, set_capacity_shrink_after_head_wraps)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(3);
    for (int i = 1; i <= 4; ++i)
    {
        q.push(std::move(i));
    }
    // Four pushes into capacity 3 leave head != 0 and values 2, 3, 4.
    EXPECT_EQ(q.size(), 3u);

    q.set_capacity(2);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.capacity(), 2u);

    auto first = q.pop();
    auto second = q.pop();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, 3);
    EXPECT_EQ(*second, 4);
    EXPECT_FALSE(q.pop());
}

TEST(reactive_circular_lossy_queue, set_capacity_grow_after_head_wraps)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(3);
    for (int i = 1; i <= 5; ++i)
    {
        q.push(std::move(i));
    }
    // Five pushes into capacity 3 leave head != 0 and values 3, 4, 5.
    EXPECT_EQ(q.size(), 3u);

    q.set_capacity(5);
    EXPECT_EQ(q.size(), 3u);
    EXPECT_EQ(q.capacity(), 5u);
    EXPECT_EQ(q.push(6), 3u);

    auto first = q.pop();
    auto second = q.pop();
    auto third = q.pop();
    auto fourth = q.pop();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    ASSERT_TRUE(fourth);
    EXPECT_EQ(*first, 3);
    EXPECT_EQ(*second, 4);
    EXPECT_EQ(*third, 5);
    EXPECT_EQ(*fourth, 6);
    EXPECT_FALSE(q.pop());
}

TEST(reactive_circular_lossy_queue, pop_empty_returns_nullopt)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(1);
    EXPECT_FALSE(q.pop());
}

TEST(reactive_circular_lossy_queue, push_without_capacity_returns_zero)
{
    reactive_circular_lossy_queue<int> q;
    EXPECT_EQ(q.push(1), 0u);
    EXPECT_EQ(q.size(), 0u);
}

TEST(reactive_circular_lossy_queue, push_without_capacity_consumes_argument)
{
    reactive_circular_lossy_queue<std::unique_ptr<int>> q;
    auto value = std::make_unique<int>(5);
    EXPECT_EQ(q.push(std::move(value)), 0u);
    EXPECT_FALSE(value);
    EXPECT_EQ(q.size(), 0u);
}

TEST(reactive_circular_lossy_queue, overrun_detected_from_push_return)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);
    const size_t capacity = q.capacity();

    EXPECT_FALSE(push_dropped(q.push(1), capacity));
    EXPECT_FALSE(push_dropped(q.push(2), capacity));
    EXPECT_TRUE(push_dropped(q.push(3), capacity));
    EXPECT_TRUE(push_dropped(q.push(4), capacity));

    auto first = q.pop();
    auto second = q.pop();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, 3);
    EXPECT_EQ(*second, 4);
}

TEST(reactive_circular_lossy_queue, push_returns_size_before_push)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);

    EXPECT_EQ(q.push(1), 0u);
    EXPECT_EQ(q.size(), 1u);
    EXPECT_EQ(q.push(2), 1u);
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.push(99), 2u);
    EXPECT_EQ(q.size(), 2u);
}

TEST(reactive_circular_lossy_queue, non_default_constructible_element)
{
    reactive_circular_lossy_queue<non_default_constructible> q;
    q.set_capacity(2);
    EXPECT_EQ(q.push(non_default_constructible{10}), 0u);
    EXPECT_EQ(q.push(non_default_constructible{20}), 1u);

    auto out = q.pop();
    ASSERT_TRUE(out);
    EXPECT_EQ(out->value, 10);
}

TEST(reactive_circular_lossy_queue, move_only_element)
{
    reactive_circular_lossy_queue<std::unique_ptr<int>> q;
    q.set_capacity(1);
    EXPECT_EQ(q.push(std::make_unique<int>(7)), 0u);

    auto out = q.pop();
    ASSERT_TRUE(out);
    ASSERT_TRUE(*out);
    EXPECT_EQ(**out, 7);
}

TEST(reactive_circular_lossy_queue, move_only_overwrite_drops_oldest)
{
    reactive_circular_lossy_queue<std::unique_ptr<int>> q;
    q.set_capacity(1);
    EXPECT_EQ(q.push(std::make_unique<int>(1)), 0u);
    EXPECT_EQ(q.push(std::make_unique<int>(2)), 1u);

    auto out = q.pop();
    ASSERT_TRUE(out);
    ASSERT_TRUE(*out);
    EXPECT_EQ(**out, 2);
    EXPECT_FALSE(q.pop());
}

TEST(reactive_circular_lossy_queue, move_construct_only_overwrite)
{
    reactive_circular_lossy_queue<move_construct_only> q;
    q.set_capacity(1);
    EXPECT_EQ(q.push(move_construct_only{1}), 0u);
    EXPECT_EQ(q.push(move_construct_only{2}), 1u);

    auto out = q.pop();
    ASSERT_TRUE(out);
    EXPECT_EQ(out->value, 2);
}

TEST(reactive_circular_lossy_queue, dropped_destructor_reenters_queue)
{
    reactive_circular_lossy_queue<reentrant_element> q;
    q.set_capacity(1);
    EXPECT_EQ(q.push(reentrant_element{&q, 1}), 0u);
    EXPECT_EQ(q.push(reentrant_element{&q, 2}), 1u);

    auto out = q.pop();
    ASSERT_TRUE(out);
    EXPECT_EQ(out->value, 2);

    EXPECT_EQ(q.push(reentrant_element{&q, 3}), 0u);
    q.set_capacity(0);
    EXPECT_EQ(q.size(), 0u);
    EXPECT_FALSE(q.has_data());
}

TEST(reactive_circular_lossy_queue, has_data_reflects_occupancy)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);
    EXPECT_FALSE(q.has_data());
    q.push(1);
    EXPECT_TRUE(q.has_data());
    EXPECT_TRUE(q.pop());
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

TEST(reactive_circular_lossy_queue, notify_callback_without_handler_is_noop)
{
    reactive_circular_lossy_queue<int> q;
    q.notify_callback();
}

TEST(reactive_circular_lossy_queue, notify_callback_may_replace_handler)
{
    reactive_circular_lossy_queue<int> q;
    int calls = 0;
    q.set_callback([&q, &calls]() {
        ++calls;
        q.set_callback([&calls]() { calls += 10; });
    });
    q.notify_callback();
    EXPECT_EQ(calls, 1);
    q.notify_callback();
    EXPECT_EQ(calls, 11);
}

TEST(reactive_circular_lossy_queue, cv_reactor_registry_alias)
{
    auto queue = std::make_shared<cv_reactor_circular_lossy_queue_tag<>::type_t>();
    auto& registry = cv_reactor_circular_lossy_queue_registry<>::get_instance();
    EXPECT_TRUE(registry.add("lossy-events", queue));
    EXPECT_EQ(registry.get("lossy-events"), queue);
    EXPECT_TRUE(registry.rem("lossy-events"));
}

TEST(reactive_circular_lossy_queue, cv_reactor_drains_surviving_elements)
{
    using reactor_t = cv_reactor<light_function<void()>>;
    reactor_t reactor;
    reactive_circular_lossy_queue<int> queue;
    queue.set_capacity(2);

    std::vector<int> got;
    std::atomic<bool> done{false};
    struct drain_ctx
    {
        reactor_t* reactor;
        reactive_circular_lossy_queue<int>* queue;
        std::vector<int>* got;
        std::atomic<bool>* done;
    } ctx{&reactor, &queue, &got, &done};

    reactor.add_read_rdy(queue, [&ctx]() {
        while (auto value = ctx.queue->pop())
        {
            ctx.got->push_back(*value);
        }
        ctx.done->store(true, std::memory_order_release);
        ctx.reactor->stop();
    });

    std::thread reactor_thread([&reactor]() { reactor.run(); });

    queue.push(1);
    queue.push(2);
    queue.push(3);
    reactor.wake_up();

    const bool finished = wait_until(done);
    if (!finished)
    {
        reactor.stop();
    }
    reactor_thread.join();

    ASSERT_TRUE(finished);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], 2);
    EXPECT_EQ(got[1], 3);
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

    auto first = q.pop();
    auto second = q.pop();
    auto third = q.pop();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    EXPECT_EQ(*first, 4);
    EXPECT_EQ(*second, 5);
    EXPECT_EQ(*third, 6);
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
