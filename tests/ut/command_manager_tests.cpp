#include <gtest/gtest.h>

#include <bfc/command_manager.hpp>

using namespace bfc;

TEST(command_manager, ShouldExecute)
{
    command_manager man;
    man.execute("window fd =1 trigger = manual size= 40");
}

TEST(command_manager, ShouldFillArgs)
{
    command_manager man;
    int fd;
    std::string trigger;
    int size;
    auto fn = [&](args_map&& pArgs) -> bfc::string
    {
        fd = *pArgs.as<int>("fd");
        trigger = *pArgs.as<std::string>("trigger");
        size = *pArgs.as<int>("size");
        return "OK";
    };
    man.add("window", fn);
    ASSERT_EQ("OK", man.execute("window fd =1 trigger = manual size= 40"));
    EXPECT_EQ(1, fd);
    EXPECT_EQ("manual", trigger);
    EXPECT_EQ(40, size);
}
