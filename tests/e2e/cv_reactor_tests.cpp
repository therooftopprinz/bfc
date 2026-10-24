#include "bfc/function.hpp"
#include <gtest/gtest.h>
#include <bfc/cv_reactor.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace bfc;

using r_cb_t = light_function<void()>;

struct fake_reactor
{
    using callback_t = r_cb_t;
    void wake_up(r_cb_t = nullptr){};
};

using queue_t = reactive_event_queue<uint64_t, r_cb_t>;

struct counters_t
{
    uint64_t server_read = 0;
    uint64_t server_write = 0;
    uint64_t client_read = 0;
    uint64_t client_write = 0;
};

constexpr uint64_t N = 1000000;

namespace {

bool wait_until(const std::atomic<bool>& flag,
                std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
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

using event_queue_t = event_queue<uint64_t>;

TEST(cv_reactor, eq_blocking_st)
{
    event_queue_t q;

    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    for (uint64_t i=0; i<N; i++)
    {
        q.push(i);
        auto j = q.pop().back();
        ASSERT_EQ(i,j);
    }
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("tput: %lf\n", tput);
}
 

TEST(cv_reactor, eq_blocking_mt)
{
    event_queue_t q;
    std::thread writer = std::thread([&q](){
            for (uint64_t i=0; i<N; i++)
            {
                q.push(i);
            }
        });

    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    uint64_t n = 0;
    while (n < N)
    {
        n += q.pop().size();
    }
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    ASSERT_EQ(n, N);

    writer.join();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("tput: %lf\n", tput);
}

TEST(cv_reactor, eq_nonblocking_mt)
{
    event_queue_t q{false};
    std::thread writer = std::thread([&q](){
            for (uint64_t i=0; i<N; i++)
            {
                q.push(i);
            }
        });

    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    uint64_t n = 0;
    while (n < N)
    {
        n += q.pop().size();
    }
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    ASSERT_EQ(n, N);

    writer.join();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("tput: %lf\n", tput);
}

TEST(cv_reactor, non_reactive_st)
{
    queue_t q;
    uint64_t n = 0;
    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    for (uint64_t i=0; i<N; i++)
    {
        q.push(i);
        auto res = q.pop();
        n += res.size();
    }
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    ASSERT_EQ(n, N);
    
    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("tput: %lf\n", tput);
}
 

TEST(cv_reactor, non_reactive_mt)
{
    queue_t q;
    std::atomic_bool start=false;
    std::thread writer = std::thread([&q, &start](){
            start = true;
            for (uint64_t i=0; i<N; i++)
            {
                q.push(i);
            }
        });

    while (!start);
    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    uint64_t n = 0;
    while (n < N)
    {
        auto r = q.pop();
        n += r.size();
    }
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    writer.join();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("tput: %lf\n", tput);
}

TEST(cv_reactor, is_reactor_thread)
{
    cv_reactor<r_cb_t> reactor;
    reactive_event_queue<uint64_t, r_cb_t> queue;

    EXPECT_FALSE(reactor.is_reactor_thread());

    std::atomic<bool> checked_in_callback{false};

    reactor.add_read_rdy(queue, [&](){
        EXPECT_TRUE(reactor.is_reactor_thread());
        checked_in_callback.store(true, std::memory_order_release);
        reactor.stop();
    });

    std::thread reactor_thread([&](){
        reactor.run();
    });

    queue.push(1);
    reactor.wake_up();

    while (!checked_in_callback.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    reactor_thread.join();

    EXPECT_FALSE(reactor.is_reactor_thread());
}

// Teardown guards ask is_reactor_thread() from other threads while run()
// starts and exits; the thread id must be safe to read concurrently (TSan).
TEST(cv_reactor, is_reactor_thread_concurrent_with_run)
{
    cv_reactor<r_cb_t> reactor;
    std::atomic<bool> polling{true};
    std::atomic<bool> other_saw_true{false};

    std::thread other([&](){
        while (polling.load(std::memory_order_acquire))
        {
            if (reactor.is_reactor_thread())
            {
                other_saw_true.store(true, std::memory_order_relaxed);
            }
        }
    });

    for (int i = 0; i < 20; i++)
    {
        std::thread reactor_thread([&](){
            reactor.run();
        });
        reactor.wake_up([&](){
            reactor.stop();
        });
        reactor_thread.join();
    }
    polling.store(false, std::memory_order_release);
    other.join();

    EXPECT_FALSE(other_saw_true.load());
    EXPECT_FALSE(reactor.is_reactor_thread());
}

TEST(cv_reactor, wake_up_cb_runs_on_reactor_thread)
{
    cv_reactor<r_cb_t> reactor;

    std::atomic<bool> done_cb_called{false};
    std::thread::id reactor_thread_id;
    std::thread::id done_cb_thread_id;

    std::thread reactor_thread([&](){
        reactor_thread_id = std::this_thread::get_id();
        reactor.run();
    });

    reactor.wake_up([&](){
        EXPECT_TRUE(reactor.is_reactor_thread());
        done_cb_called.store(true, std::memory_order_release);
        done_cb_thread_id = std::this_thread::get_id();
        reactor.stop();
    });

    ASSERT_TRUE(wait_until(done_cb_called));
    reactor_thread.join();

    EXPECT_EQ(done_cb_thread_id, reactor_thread_id);
}

TEST(cv_reactor, wake_up_cb_before_run)
{
    cv_reactor<r_cb_t> reactor;
    std::atomic<bool> called{false};

    reactor.wake_up([&](){
        called.store(true, std::memory_order_release);
        reactor.stop();
    });

    reactor.run();

    EXPECT_TRUE(called.load(std::memory_order_acquire));
}

TEST(cv_reactor, push_without_wake_up_requires_explicit_wakeup)
{
    cv_reactor<r_cb_t> reactor(200);
    reactive_event_queue<uint64_t, r_cb_t> queue;

    std::atomic<bool> callback_called{false};
    std::atomic<bool> reactor_started{false};

    reactor.add_read_rdy(queue, [&](){
        callback_called.store(true, std::memory_order_release);
    });

    std::thread reactor_thread([&](){
        reactor_started.store(true, std::memory_order_release);
        reactor.run();
    });

    ASSERT_TRUE(wait_until(reactor_started));

    queue.push(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    EXPECT_FALSE(callback_called.load(std::memory_order_acquire));

    reactor.wake_up();
    ASSERT_TRUE(wait_until(callback_called));

    reactor.stop();
    reactor_thread.join();
}

TEST(cv_reactor, reactive_st)
{
    cv_reactor<r_cb_t> reactor;
    reactive_event_queue<uint64_t, r_cb_t> queue;

    uint64_t i = 0;
    reactor.add_read_rdy(queue, [&reactor, &queue, &i](){
            auto rv = queue.pop();
            if (i>=N)
            {
                reactor.stop();
                return;
            }
            queue.push(i++);
            reactor.wake_up();
        });

    queue.push(i++);
    reactor.wake_up();
    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    reactor.run();
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("tput: %lf\n", tput);
    
}

TEST(cv_reactor, reactive_mt)
{
    cv_reactor<r_cb_t> reactor;
    reactive_event_queue<uint64_t, r_cb_t> queue;

    reactor.add_read_rdy(queue, [&reactor, &queue](){
            auto rv = queue.pop();
            if (rv.size())
            {
                auto i = rv.back();
                if (i>=(N-1))
                {
                    reactor.stop();
                }
            }
        });

    std::thread writer = std::thread([&reactor, &queue](){
            for (uint64_t i=0; i<N; i++)
            {
                queue.push(i);
                reactor.wake_up();
            }
        });

    auto t_start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    reactor.run();
    auto t_end = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    writer.join();

    auto t_diff = (t_end - t_start);
    auto tput = double(N) * 1000 * 1000 * 1000 / t_diff;
    tput /= 1000000;

    printf("tput: %lf\n", tput);
}

TEST(cv_reactor, minimum_timer_step)
{
#if defined(__linux__)
    static_assert(bfc::cv_reactor<>::minimum_timer_step() == 1);
#else
    static_assert(bfc::cv_reactor<>::minimum_timer_step() == 1000);
#endif
}

// A timer due before the idle timeout must not wait out the whole timeout
// (the deadline was compared in us against a ms timeout).
TEST(cv_reactor, timer_before_idle_timeout_fires_on_time)
{
    bfc::cv_reactor<std::function<void()>> reactor(100);
    auto t0 = std::chrono::steady_clock::now();
    reactor.get_timer().wait_ms(5, [&](){ reactor.stop(); });
    reactor.run();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    EXPECT_GE(elapsed.count(), 5);
    EXPECT_LT(elapsed.count(), 50);
}

TEST(cv_reactor, sub_millisecond_timer_chain)
{
    bfc::cv_reactor<std::function<void()>> reactor(100);
    auto t0 = std::chrono::steady_clock::now();
    int fired = 0;
    for (int i = 0; i < 100; ++i)
    {
        reactor.get_timer().wait_us(250 * int64_t(i + 1), [&](){ ++fired; });
    }
    reactor.get_timer().wait_us(250 * 100 + 500, [&](){ reactor.stop(); });
    reactor.run();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    EXPECT_EQ(fired, 100);
    EXPECT_LT(elapsed.count(), 80);
}
