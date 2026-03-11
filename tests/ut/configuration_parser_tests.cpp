#include <gtest/gtest.h>

#include <bfc/configuration_parser.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace bfc;

TEST(configuration_parser, shouldParseConfig)
{
    configuration_parser parser;
    parser.load_line("key1 = 1234");
    parser.load_line("key2 = 12.34");
    parser.load_line("key3 = qwerty");

    ASSERT_TRUE(parser.as<int>("key1").has_value());
    EXPECT_EQ(1234, parser.as<int>("key1").value());
    EXPECT_EQ(4321, parser.as<int>("key12").value_or(4321));
    ASSERT_TRUE(parser.as<double>("key2").has_value());
    EXPECT_EQ(12.34, parser.as<double>("key2").value());
    ASSERT_TRUE(parser.arg("key3").has_value());
    EXPECT_EQ("qwerty", parser.arg("key3").value());
    EXPECT_FALSE(parser.arg("key31").has_value());
    EXPECT_FALSE(parser.as<int>("key3").has_value());
}

TEST(configuration_parser, shouldTrimKeysAndValues)
{
    configuration_parser parser;
    parser.load_line("  key1  =  1234  ");
    parser.load_line("\tkey2\t=\tvalue with spaces\t");

    ASSERT_TRUE(parser.as<int>("key1").has_value());
    EXPECT_EQ(1234, parser.as<int>("key1").value());
    ASSERT_TRUE(parser.arg("key2").has_value());
    EXPECT_EQ("value with spaces", parser.arg("key2").value());
}

TEST(configuration_parser, shouldLookupWithStringView)
{
    configuration_parser parser;
    parser.load_line("mykey = 42");
    std::string key_storage("mykey");
    std::string_view key(key_storage);
    ASSERT_TRUE(parser.as<int>(key).has_value());
    EXPECT_EQ(42, parser.as<int>(key).value());
    ASSERT_TRUE(parser.arg(key).has_value());
    EXPECT_EQ("42", parser.arg(key).value());
}

TEST(configuration_parser, loadReturnsFalseWhenFileMissing)
{
    configuration_parser parser;
    EXPECT_FALSE(parser.load("/nonexistent/path/bfc_no_such_file"));
    EXPECT_TRUE(parser.empty());
}

TEST(configuration_parser, loadReturnsTrueAndParsesFile)
{
    std::string path = "/tmp/bfc_config_parser_test_" + std::to_string(getpid());
    std::ofstream out(path);
    ASSERT_TRUE(out.is_open());
    out << "a = 1\n";
    out << "b = 2\n";
    out << "name = from_file\n";
    out.close();

    configuration_parser parser;
    EXPECT_TRUE(parser.load(path));
    ASSERT_TRUE(parser.as<int>("a").has_value());
    EXPECT_EQ(1, parser.as<int>("a").value());
    ASSERT_TRUE(parser.as<int>("b").has_value());
    EXPECT_EQ(2, parser.as<int>("b").value());
    ASSERT_TRUE(parser.arg("name").has_value());
    EXPECT_EQ("from_file", parser.arg("name").value());

    std::remove(path.c_str());
}

TEST(configuration_parser, behavesLikeMap)
{
    configuration_parser parser;
    parser.load_line("x = 10");
    parser["y"] = "20";
    parser.emplace("z", "30");

    EXPECT_EQ(3u, parser.size());
    EXPECT_EQ("10", parser["x"]);
    EXPECT_EQ("20", parser["y"]);
    EXPECT_EQ("30", parser["z"]);
    EXPECT_NE(parser.find("x"), parser.end());
    EXPECT_EQ(parser.find("missing"), parser.end());
}
