#include <gtest/gtest.h>

#include <bfc/bidirectional_queue_registry.hpp>
#include <bfc/event_queue.hpp>
#include <bfc/function.hpp>

using namespace bfc;

using test_queue_t =
    reactive_event_queue<uint64_t, light_function<void()>>;

TEST(BidirectionalQueueRegistry, AbAttachPushPopBothLegs)
{
    auto& registry = bidirectional_queue_registry<test_queue_t>::instance();
    const char kName[] = "test-link";

    int endpoint_a = 1;
    int endpoint_b = 2;
    bidirectional_queue_endpoint_view<test_queue_t> view_a{};
    bidirectional_queue_endpoint_view<test_queue_t> view_b{};

    ASSERT_TRUE(registry.endpoint_for(
        kName, bidirectional_queue_role::a, &endpoint_a, &view_a));
    ASSERT_TRUE(registry.endpoint_for(
        kName, bidirectional_queue_role::b, &endpoint_b, &view_b));

    view_a.outbound->push(42u);
    auto from_a = view_b.inbound->pop();
    ASSERT_EQ(from_a.size(), 1u);
    EXPECT_EQ(from_a[0], 42u);

    view_b.outbound->push(99u);
    auto from_b = view_a.inbound->pop();
    ASSERT_EQ(from_b.size(), 1u);
    EXPECT_EQ(from_b[0], 99u);
}

TEST(BidirectionalQueueRegistry, DuplicateRoleAttachFails)
{
    auto& registry = bidirectional_queue_registry<test_queue_t>::instance();
    const char kName[] = "dup-role-link";

    int endpoint_a = 1;
    int endpoint_a2 = 2;
    bidirectional_queue_endpoint_view<test_queue_t> view{};

    ASSERT_TRUE(registry.endpoint_for(
        kName, bidirectional_queue_role::a, &endpoint_a, &view));
    EXPECT_FALSE(registry.endpoint_for(
        kName, bidirectional_queue_role::a, &endpoint_a2, &view));
}

TEST(BidirectionalQueueRegistry, GetOrCreateSharesStorage)
{
    auto& registry = bidirectional_queue_registry<test_queue_t>::instance();
    const char kName[] = "shared-link";

    auto link1 = registry.get_or_create(kName);
    auto link2 = registry.get_or_create(kName);
    ASSERT_TRUE(link1);
    ASSERT_TRUE(link2);
    EXPECT_EQ(link1.get(), link2.get());

    int endpoint_a = 1;
    int endpoint_b = 2;
    bidirectional_queue_endpoint_view<test_queue_t> view_a{};
    bidirectional_queue_endpoint_view<test_queue_t> view_b{};
    ASSERT_TRUE(registry.endpoint_for(
        kName, bidirectional_queue_role::a, &endpoint_a, &view_a));
    ASSERT_TRUE(registry.endpoint_for(
        kName, bidirectional_queue_role::b, &endpoint_b, &view_b));

    EXPECT_EQ(view_a.outbound, &link1->a);
    EXPECT_EQ(view_a.inbound, &link1->b);
    EXPECT_EQ(view_b.outbound, &link1->b);
    EXPECT_EQ(view_b.inbound, &link1->a);
}
