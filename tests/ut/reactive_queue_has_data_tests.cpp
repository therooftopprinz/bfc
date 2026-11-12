// has_data() is lock-free (an atomic size written under the queue mutex): it
// must track every change of the queue's contents and be safe to poll from
// another thread, as cv_reactor does.
#include <gtest/gtest.h>

#include <bfc/event_queue.hpp>

#include <atomic>
#include <thread>

using namespace bfc;

TEST(reactive_queue_has_data, event_queue_tracks_push_and_pop)
{
    reactive_event_queue<int> q;
    EXPECT_FALSE(q.has_data());
    q.push(1);
    EXPECT_TRUE(q.has_data());
    q.push(2);
    auto all = q.pop();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_FALSE(q.has_data());
    EXPECT_EQ(q.size(), 0u);
    q.push(3);
    EXPECT_TRUE(q.has_data());
}

TEST(reactive_queue_has_data, lossy_queue_tracks_overwrite_and_resize)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(2);
    EXPECT_FALSE(q.has_data());
    q.push(1);
    q.push(2);
    q.push(3);  // overwrites the oldest: still two
    EXPECT_TRUE(q.has_data());
    q.set_capacity(1);  // keeps the newest one
    EXPECT_TRUE(q.has_data());
    EXPECT_EQ(*q.pop(), 3);
    EXPECT_FALSE(q.has_data());
    q.push(4);
    q.set_capacity(0);  // keeps nothing
    EXPECT_FALSE(q.has_data());
    q.push(5);  // capacity 0 discards
    EXPECT_FALSE(q.has_data());
}

// A consumer that polls only has_data() (like cv_reactor's scan) must see
// every item a producer pushes on another thread.
TEST(reactive_queue_has_data, lossy_queue_polled_across_threads)
{
    reactive_circular_lossy_queue<int> q;
    q.set_capacity(64);
    constexpr int n = 20000;
    std::atomic<bool> done{false};
    long long sum = 0;
    int got = 0;
    std::thread consumer([&] {
        while (got < n)
        {
            if (!q.has_data())
            {
                if (done.load(std::memory_order_acquire) && !q.has_data())
                {
                    break;
                }
                std::this_thread::yield();
                continue;
            }
            while (auto v = q.pop())
            {
                sum += *v;
                ++got;
            }
        }
    });
    for (int i = 1; i <= n; ++i)
    {
        while (q.size() >= 60)  // never overwrite: every item must arrive
        {
            std::this_thread::yield();
        }
        q.push(int(i));
    }
    done.store(true, std::memory_order_release);
    consumer.join();
    EXPECT_EQ(got, n);
    EXPECT_EQ(sum, (long long)n * (n + 1) / 2);
    EXPECT_FALSE(q.has_data());
}

TEST(reactive_queue_has_data, event_queue_polled_across_threads)
{
    reactive_event_queue<int> q;
    constexpr int n = 20000;
    std::atomic<bool> done{false};
    int got = 0;
    std::thread consumer([&] {
        for (;;)
        {
            if (q.has_data())
            {
                got += int(q.pop().size());
                continue;
            }
            if (done.load(std::memory_order_acquire) && !q.has_data())
            {
                break;
            }
            std::this_thread::yield();
        }
    });
    for (int i = 0; i < n; ++i)
    {
        q.push(i);
    }
    done.store(true, std::memory_order_release);
    consumer.join();
    EXPECT_EQ(got, n);
}
