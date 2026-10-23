#include <bfc/cv_reactor.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace {
struct wake_target
{
    std::function<void()> on_wake;
    void wake_up() { on_wake(); }
};

using vector_queue = bfc::reactive_event_queue<int, bfc::light_function<void()>>;
using lossy_queue = bfc::reactive_circular_lossy_queue<int>;

void prepare(vector_queue&) {}
void prepare(lossy_queue& queue) { queue.set_capacity(8); }

template <typename Queue>
void check_binding()
{
    Queue queue;
    prepare(queue);
    int first_wakes = 0;
    int second_wakes = 0;
    auto first = std::make_shared<wake_target>();
    first->on_wake = [&] {
        EXPECT_TRUE(queue.has_data()); // Notification must release the queue mutex.
        ++first_wakes;
    };
    auto second = std::make_shared<wake_target>();
    second->on_wake = [&] { ++second_wakes; };
    queue.push(0);
    EXPECT_EQ(first_wakes, 0);
    queue.set_reactor(first);
    queue.push(1);
    queue.push(2);
    EXPECT_EQ(first_wakes, 2);
    queue.set_reactor(second);
    queue.push(3);
    EXPECT_EQ(first_wakes, 2);
    EXPECT_EQ(second_wakes, 1);
    queue.set_reactor(nullptr);
    queue.push(4);
    EXPECT_EQ(second_wakes, 1);
    queue.set_reactor(first);
    queue.set_reactor(std::shared_ptr<wake_target>{});
    queue.push(5);
    EXPECT_EQ(first_wakes, 2);
}

template <typename Queue>
void check_reactor_delivery()
{
    auto reactor = std::make_shared<bfc::cv_reactor<>>(5000);
    Queue queue;
    prepare(queue);
    queue.set_reactor(reactor);
    std::promise<void> delivered;
    auto result = delivered.get_future();
    reactor->add_read_rdy(queue, [&] {
        queue.pop();
        delivered.set_value();
        reactor->stop();
    });
    std::promise<void> started;
    auto ready = started.get_future();
    reactor->wake_up([&] { started.set_value(); });
    std::thread worker([&] { reactor->run(); });
    const auto start_status = ready.wait_for(std::chrono::seconds(2));
    // The start wake's queue scan has completed before this callback runs.
    queue.push(42);
    const auto status = result.wait_for(std::chrono::seconds(2));
    reactor->stop();
    worker.join();
    EXPECT_EQ(start_status, std::future_status::ready);
    EXPECT_EQ(status, std::future_status::ready);
}

template <typename Queue>
void check_concurrent_rebinding()
{
    Queue queue;
    prepare(queue);
    std::atomic<int> notifications{0};
    auto target = std::make_shared<wake_target>();
    target->on_wake = [&] { ++notifications; };
    queue.set_reactor(target);
    std::thread producer([&] {
        for (int i = 0; i < 10000; ++i) queue.push(int(i));
    });
    for (int i = 0; i < 1000; ++i)
    {
        queue.set_reactor(nullptr);
        queue.set_reactor(target);
    }
    producer.join();
    const int before = notifications.load();
    queue.push(10001);
    EXPECT_EQ(notifications.load(), before + 1);
}
} // namespace

TEST(reactive_queue_wakeup, vector_binding_replace_clear) { check_binding<vector_queue>(); }
TEST(reactive_queue_wakeup, lossy_binding_replace_clear) { check_binding<lossy_queue>(); }
TEST(reactive_queue_wakeup, vector_dispatch_without_manual_wake) { check_reactor_delivery<vector_queue>(); }
TEST(reactive_queue_wakeup, lossy_dispatch_without_manual_wake) { check_reactor_delivery<lossy_queue>(); }
TEST(reactive_queue_wakeup, vector_concurrent_rebinding) { check_concurrent_rebinding<vector_queue>(); }
TEST(reactive_queue_wakeup, lossy_concurrent_rebinding) { check_concurrent_rebinding<lossy_queue>(); }

TEST(reactive_queue_wakeup, lossy_overwrite_wakes_but_zero_capacity_does_not)
{
    lossy_queue queue;
    int wakes = 0;
    auto target = std::make_shared<wake_target>();
    target->on_wake = [&] { ++wakes; };
    queue.set_reactor(target);
    queue.push(1);
    EXPECT_EQ(wakes, 0);
    queue.set_capacity(1);
    queue.push(2);
    queue.push(3);
    EXPECT_EQ(wakes, 2);
    EXPECT_EQ(*queue.pop(), 3);
}

TEST(reactive_queue_wakeup, binding_retains_reactor_until_cleared_or_queue_destroyed)
{
    std::weak_ptr<wake_target> weak;
    {
        vector_queue queue;
        auto target = std::make_shared<wake_target>();
        target->on_wake = [] {};
        weak = target;
        queue.set_reactor(target);
        target.reset();
        EXPECT_FALSE(weak.expired());
        queue.push(1);
        queue.set_reactor(nullptr);
        EXPECT_TRUE(weak.expired());
        target = std::make_shared<wake_target>();
        weak = target;
        queue.set_reactor(target);
        target.reset();
        EXPECT_FALSE(weak.expired());
    }
    EXPECT_TRUE(weak.expired());
}

TEST(reactive_queue_wakeup, clearing_binding_during_push_keeps_inflight_reactor_alive)
{
    vector_queue queue;
    std::promise<void> entered;
    auto entry = entered.get_future();
    std::promise<void> release;
    auto released = release.get_future();
    auto target = std::make_shared<wake_target>();
    target->on_wake = [&] {
        entered.set_value();
        released.wait();
    };
    std::weak_ptr<wake_target> weak = target;
    queue.set_reactor(target);
    target.reset();
    std::thread producer([&] { queue.push(1); });
    const auto status = entry.wait_for(std::chrono::seconds(2));
    queue.set_reactor(nullptr);
    EXPECT_FALSE(weak.expired());
    release.set_value();
    producer.join();
    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_TRUE(weak.expired());
}

TEST(reactive_queue_wakeup, notification_can_clear_its_own_binding)
{
    vector_queue queue;
    auto target = std::make_shared<wake_target>();
    int wakes = 0;
    target->on_wake = [&] { ++wakes; queue.set_reactor(nullptr); };
    queue.set_reactor(target);
    queue.push(1);
    queue.push(2);
    EXPECT_EQ(wakes, 1);
}
