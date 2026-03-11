#include <gtest/gtest.h>
#include <bfc/cv_reactor.hpp>

#include <deque>
#include <thread>

using namespace bfc;

using r_cb_t = std::function<void()>;

struct fake_reactor
{
    using callback_t = r_cb_t;
    void wake_up(r_cb_t = nullptr){};
};

using queue_t = reactive_event_queue<uint64_t>;

struct counters_t
{
    uint64_t server_read = 0;
    uint64_t server_write = 0;
    uint64_t client_read = 0;
    uint64_t client_write = 0;
};

constexpr uint64_t N = 1000000;

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

TEST(cv_reactor, reactive_st)
{
    cv_reactor<> reactor;
    reactive_event_queue<uint64_t> queue;

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
    cv_reactor<> reactor;
    reactive_event_queue<uint64_t> queue;

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
