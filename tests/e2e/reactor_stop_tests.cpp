#include <gtest/gtest.h>
#include <bfc/cv_reactor.hpp>
#include <bfc/epoll_reactor.hpp>
#include <bfc/poll_reactor.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

template <typename reactor_t>
class reactor_stop : public ::testing::Test
{
};

using reactor_stop_types =
    ::testing::Types<bfc::cv_reactor<>, bfc::poll_reactor<>, bfc::epoll_reactor<>>;
TYPED_TEST_CASE(reactor_stop, reactor_stop_types);

namespace
{

/* Runs the reactor on a thread; true when run() returned within the timeout. If it did not,
 * stops it again so the thread can be joined (the test then fails instead of hanging). */
template <typename reactor_t>
bool run_returns(reactor_t& reactor, std::chrono::milliseconds timeout)
{
    std::promise<void> returned;
    auto               done = returned.get_future();
    std::thread        th([&] {
        reactor.run();
        returned.set_value();
    });
    const bool ok = done.wait_for(timeout) == std::future_status::ready;
    while (done.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
    {
        reactor.stop();
    }
    th.join();
    return ok;
}

} // namespace

// A stop() that comes before the reactor thread enters run() must still end it.
TYPED_TEST(reactor_stop, stop_before_run_ends_run)
{
    TypeParam reactor;
    reactor.stop();
    EXPECT_TRUE(run_returns(reactor, std::chrono::seconds(2)));
}

// The stop request is consumed by the run() it ends: the reactor runs again afterwards.
TYPED_TEST(reactor_stop, runs_again_after_stop)
{
    TypeParam reactor;
    reactor.stop();
    ASSERT_TRUE(run_returns(reactor, std::chrono::seconds(2)));

    std::atomic<bool> ran {false};
    std::promise<void> returned;
    auto               done = returned.get_future();
    std::thread        th([&] {
        reactor.run();
        returned.set_value();
    });
    reactor.wake_up([&] {
        ran = true;
        reactor.stop();
    });
    EXPECT_EQ(done.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    while (done.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
    {
        reactor.stop();
    }
    th.join();
    EXPECT_TRUE(ran.load());
}

// Thread start racing a stop, as when a reactor thread is created and stopped at once.
TYPED_TEST(reactor_stop, start_and_stop_at_once)
{
    for (int i = 0; i < 200; i++)
    {
        TypeParam reactor;
        std::promise<void> returned;
        auto               done = returned.get_future();
        std::thread        th([&] {
            reactor.run();
            returned.set_value();
        });
        reactor.stop();
        const bool ok = done.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        while (done.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready)
        {
            reactor.stop();
        }
        th.join();
        ASSERT_TRUE(ok) << "iteration " << i;
    }
}
