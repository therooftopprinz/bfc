#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <bfc/timer.hpp>

using namespace bfc;
using namespace testing;

using TestTimer = timer<light_function<void()>>;

TEST(TimerTest, shouldExecuteSingleTimerAtOrAfterDeadline)
{
    TestTimer t;
    bool called = false;

    // Use microsecond timebase explicitly
    int64_t now_us = 1'000;
    t.wait_us(100, [&] { called = true; }, now_us);

    // Before deadline: nothing
    t.schedule(now_us + 50);
    EXPECT_FALSE(called);

    // At / after deadline: should fire
    t.schedule(now_us + 100);
    EXPECT_TRUE(called);
}

TEST(TimerTest, shouldExecuteInOrderOfDeadline)
{
    TestTimer t;
    std::vector<int> sequence;

    int64_t base_us = 10;

    // Later timer (larger deadline)
    t.wait_us(200, [&] { sequence.push_back(2); }, base_us);
    // Earlier timer (smaller deadline)
    t.wait_us(50, [&] { sequence.push_back(1); }, base_us);

    t.schedule(base_us + 300);

    ASSERT_THAT(sequence, ElementsAre(1, 2));
}

TEST(TimerTest, shouldNotRunCanceledTimer)
{
    TestTimer t;
    std::vector<int> sequence;
    int64_t base_us = 10;

    auto id1 = t.wait_us(50, [&] { sequence.push_back(1); }, base_us);
    auto id2 = t.wait_us(60, [&] { sequence.push_back(2); }, base_us);

    // Cancel second timer
    EXPECT_TRUE(t.cancel(id2));

    t.schedule(base_us + 100);

    ASSERT_THAT(sequence, ElementsAre(1));
}

TEST(TimerTest, cancelReturnsFalseForUnknownId)
{
    TestTimer t;
    // Arbitrary id that was never scheduled
    TestTimer::timer_id_t fake{1234, 5678};
    EXPECT_FALSE(t.cancel(fake));
}

