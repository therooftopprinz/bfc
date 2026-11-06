#include <gtest/gtest.h>
#include <bfc/default_reactor.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>

// The default reactor's callbacks are large_functions: room for an owner
// (shared_ptr), another shared_ptr and scalars, which light_function lacks.
TEST(default_reactor, callback_is_large_function)
{
    using cb_t = bfc::large_function<void()>;
    auto owner = std::make_shared<int>(7);
    auto other = std::make_shared<int>(8);
    uint64_t a = 1, b = 2;
    int fd = 3;
    cb_t cb([owner, other, a, b, fd]() { (void)owner; (void)other; (void)a; (void)b; (void)fd; });
    EXPECT_TRUE(bool(cb));
}

TEST(default_reactor, registry_and_wake_up_with_large_capture)
{
    auto reactor = std::make_shared<bfc::default_reactor<>>();
    ASSERT_TRUE(bfc::default_reactor_registry::get_instance().add("default_reactor_test", reactor));
    auto found = bfc::default_reactor_registry::get_instance().get("default_reactor_test");
    ASSERT_EQ(found, reactor);

    std::thread t([&]() { reactor->run(); });
    auto owner = std::make_shared<std::atomic<int>>(0);
    auto extra = std::make_shared<int>(41);
    uint64_t add = 1;
    found->wake_up([owner, extra, add]() { owner->store(*extra + int(add)); });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (owner->load() != 42 && std::chrono::steady_clock::now() < until)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    reactor->stop();
    t.join();
    EXPECT_EQ(owner->load(), 42);
    bfc::default_reactor_registry::get_instance().rem("default_reactor_test");
}
