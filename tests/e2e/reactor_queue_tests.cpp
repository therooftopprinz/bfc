// Reactive queues on the fd reactors (poll_reactor, epoll_reactor), as
// cv_reactor serves them: a push wakes the reactor, the queue's cb runs while
// it has data, and a backlog left by a cb is served without blocking.
#include <gtest/gtest.h>
#include <bfc/event_queue.hpp>
#include <bfc/poll_reactor.hpp>
#include <bfc/epoll_reactor.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace
{

using queue_t = bfc::reactive_circular_lossy_queue<int>;

template <typename reactor_t>
void drains_backlog_without_more_pushes()
{
    auto reactor = std::make_shared<reactor_t>();
    queue_t queue;
    queue.set_capacity(16);
    queue.set_reactor(reactor);
    std::atomic<int> taken{0};
    std::atomic<int> calls{0};
    queue_t* q = &queue;
    std::atomic<int>* t = &taken;
    std::atomic<int>* c = &calls;
    ASSERT_TRUE(reactor->add_read_rdy(queue, [q, t, c]() {
        c->fetch_add(1);
        for (int i = 0; i < 2 && q->pop(); ++i)  // at most 2 per pass
        {
            t->fetch_add(1);
        }
    }));
    std::thread th([&]() { reactor->run(); });

    for (int i = 0; i < 5; ++i)
    {
        queue.push(int(i));
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (taken.load() != 5 && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(taken.load(), 5);

    // Empty: no more calls.
    const int calls_after = calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(calls.load(), calls_after);

    // Removed: a push no longer reaches cb.
    ASSERT_TRUE(reactor->rem_read_rdy(queue));
    queue.push(99);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(taken.load(), 5);
    EXPECT_TRUE(queue.has_data());

    reactor->stop();
    th.join();
    queue.set_reactor(nullptr);
}

template <typename reactor_t>
void data_queued_before_registration_is_served()
{
    auto reactor = std::make_shared<reactor_t>();
    std::thread th([&]() { reactor->run(); });
    queue_t queue;
    queue.set_capacity(4);
    queue.push(1);
    queue.set_reactor(reactor);
    std::atomic<int> taken{0};
    queue_t* q = &queue;
    std::atomic<int>* t = &taken;
    ASSERT_TRUE(reactor->add_read_rdy(queue, [q, t]() {
        while (q->pop())
        {
            t->fetch_add(1);
        }
    }));
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (taken.load() != 1 && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_EQ(taken.load(), 1);
    reactor->rem_read_rdy(queue);
    reactor->stop();
    th.join();
    queue.set_reactor(nullptr);
}

}  // namespace

TEST(poll_reactor, queue_backlog_is_drained_without_more_pushes)
{
    drains_backlog_without_more_pushes<bfc::poll_reactor<bfc::large_function<void()>>>();
}

TEST(poll_reactor, queue_data_before_registration_is_served)
{
    data_queued_before_registration_is_served<bfc::poll_reactor<bfc::large_function<void()>>>();
}

TEST(epoll_reactor, queue_backlog_is_drained_without_more_pushes)
{
    drains_backlog_without_more_pushes<bfc::epoll_reactor<bfc::large_function<void()>>>();
}

TEST(epoll_reactor, queue_data_before_registration_is_served)
{
    data_queued_before_registration_is_served<bfc::epoll_reactor<bfc::large_function<void()>>>();
}
