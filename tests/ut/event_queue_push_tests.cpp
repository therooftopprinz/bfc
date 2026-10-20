#include <gtest/gtest.h>

#include <bfc/event_queue.hpp>

using namespace bfc;

TEST(event_queue, push_returns_size_after_push)
{
    event_queue<int> q{false};

    EXPECT_EQ(q.push(10), 1u);
    EXPECT_EQ(q.size(), 1u);
    EXPECT_EQ(q.push(20), 2u);
    EXPECT_EQ(q.size(), 2u);
}

TEST(reactive_event_queue, push_returns_size_after_push)
{
    reactive_event_queue<int, light_function<void()>> q;

    EXPECT_EQ(q.push(1), 1u);
    EXPECT_EQ(q.size(), 1u);
    EXPECT_EQ(q.push(2), 2u);
    EXPECT_EQ(q.size(), 2u);
}
