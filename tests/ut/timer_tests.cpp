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

    auto now = 1000;
    t.wait_ms(100, [&] { called = true; }, now);

    // Before deadline: nothing
    t.schedule(now + 50);
    EXPECT_FALSE(called);

    // At / after deadline: should fire
    t.schedule(now + 100);
    EXPECT_TRUE(called);
}

TEST(TimerTest, shouldExecuteInOrderOfDeadline)
{
    TestTimer t;
    std::vector<int> sequence;

    auto base = 10;

    // Later timer
    t.wait_ms(200, [&] { sequence.push_back(2); }, base);
    // Earlier timer
    t.wait_ms(50, [&] { sequence.push_back(1); }, base);

    t.schedule(base + 300);

    ASSERT_THAT(sequence, ElementsAre(1, 2));
}

TEST(TimerTest, shouldNotRunCanceledTimer)
{
    TestTimer t;
    std::vector<int> sequence;
    auto base = 10;

    auto id1 = t.wait_ms(50, [&] { sequence.push_back(1); }, base);
    auto id2 = t.wait_ms(60, [&] { sequence.push_back(2); }, base);

    // Cancel second timer
    EXPECT_TRUE(t.cancel(id2));

    t.schedule(base + 100);

    ASSERT_THAT(sequence, ElementsAre(1));
}

TEST(TimerTest, cancelReturnsFalseForUnknownId)
{
    TestTimer t;
    // Arbitrary id that was never scheduled
    TestTimer::timer_id_t fake{1234, 5678};
    EXPECT_FALSE(t.cancel(fake));
}

