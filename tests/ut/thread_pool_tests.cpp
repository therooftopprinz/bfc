#include <gtest/gtest.h>

#include <bfc/function.hpp>
#include <bfc/thread_pool.hpp>
#include <future>

using namespace bfc;

TEST(thread_pool, ShouldExecute)
{
    using TP = thread_pool<light_function<void()>>;
    TP pool;

    constexpr int COUNT = 50;
    std::vector<std::promise<int>> res(COUNT);
    std::vector<TP::fn_t> exec;

    for (int i=0u; i<COUNT; i++)
    {
        exec.emplace_back([&res, i](){res[i].set_value(i);});
    }

    for (auto& i : exec)
    {
        pool.execute(i);
        std::printf("Active %lu\n", pool.count_active());
    }

    for (int i=0u; i<COUNT; i++)
    {
        EXPECT_EQ(i, res[i].get_future().get());
    }
    std::printf("Pool size %lu\n", pool.size());

}
