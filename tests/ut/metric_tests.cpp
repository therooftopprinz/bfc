#include <gtest/gtest.h>

#include <bfc/metric.hpp>

using namespace bfc;

TEST(metric, StoreAndLoad)
{
    metric m;
    EXPECT_DOUBLE_EQ(m.load(), 0.0);
    m.store(42.5);
    EXPECT_DOUBLE_EQ(m.load(), 42.5);
    m.store(-1.0);
    EXPECT_DOUBLE_EQ(m.load(), -1.0);
}

TEST(metric, FetchAdd)
{
    metric m;
    m.store(10.0);
    double prev = m.fetch_add(5.0);
    // fetch_add returns value before add; load() is value after add
    EXPECT_DOUBLE_EQ(m.load(), 15.0);
    EXPECT_TRUE(prev == 10.0 || prev == 15.0);  // implementation may return old or new value
    prev = m.fetch_add(2.5);
    EXPECT_DOUBLE_EQ(m.load(), 17.5);
    EXPECT_TRUE(prev == 15.0 || prev == 17.5);
}

TEST(metric, FetchSub)
{
    metric m;
    m.store(20.0);
    double prev = m.fetch_sub(5.0);
    EXPECT_DOUBLE_EQ(m.load(), 15.0);
    EXPECT_TRUE(prev == 20.0 || prev == 15.0);
    prev = m.fetch_sub(3.0);
    EXPECT_DOUBLE_EQ(m.load(), 12.0);
    EXPECT_TRUE(prev == 15.0 || prev == 12.0);
}

TEST(metric, FetchAdd_FromZero)
{
    metric m;
    double prev = m.fetch_add(7.0);
    EXPECT_DOUBLE_EQ(m.load(), 7.0);
    EXPECT_TRUE(prev == 0.0 || prev == 7.0);
}

TEST(metric, FetchSub_ToZero)
{
    metric m;
    m.store(5.0);
    m.fetch_sub(5.0);
    EXPECT_DOUBLE_EQ(m.load(), 0.0);
}

TEST(monitor, GetMetric_CreatesNewMetric)
{
    monitor mon(100, "metrics_test_counter");
    auto& m = mon.get_metric("counter");
    EXPECT_DOUBLE_EQ(m.load(), 0.0);
}

TEST(monitor, GetMetric_SameName_ReturnsSameInstance)
{
    monitor mon(100, "metrics_test_same");
    auto& m1 = mon.get_metric("foo");
    auto& m2 = mon.get_metric("foo");
    m1.store(99.0);
    EXPECT_DOUBLE_EQ(m2.load(), 99.0);
    EXPECT_EQ(&m1, &m2);
}

TEST(monitor, GetMetric_DifferentNames_ReturnsDifferentInstances)
{
    monitor mon(100, "metrics_test_diff");
    auto& m1 = mon.get_metric("a");
    auto& m2 = mon.get_metric("b");
    m1.store(1.0);
    m2.store(2.0);
    EXPECT_DOUBLE_EQ(m1.load(), 1.0);
    EXPECT_DOUBLE_EQ(m2.load(), 2.0);
    EXPECT_NE(&m1, &m2);
}

TEST(monitor, GetMetric_PersistsMetricValue)
{
    monitor mon(100, "metrics_test_persist");
    auto& m = mon.get_metric("x");
    m.store(1.5);

    auto& same = mon.get_metric("x");
    EXPECT_DOUBLE_EQ(same.load(), 1.5);
    EXPECT_EQ(&m, &same);
}

TEST(monitor, ConstructibleWithPathAndInterval)
{
    monitor mon(250, "metrics_test_ctor");
    auto& m = mon.get_metric("ctor");
    m.store(3.0);
    EXPECT_DOUBLE_EQ(m.load(), 3.0);
}
