#include <gtest/gtest.h>

#include <bfc/buffer.hpp>

using namespace bfc;

TEST(simple_shared_buffer_view, ShouldConstructFromSharedBuffer_FullView)
{
    auto buf = std::make_shared<buffer>(new std::byte[8], 8);
    shared_buffer_view view(buf);
    EXPECT_EQ(view.data(), buf->data());
    EXPECT_EQ(view.size(), 8u);
    EXPECT_FALSE(view.empty());
}

TEST(simple_shared_buffer_view, ShouldConstructFromSharedBuffer_SubView)
{
    auto buf = std::make_shared<buffer>(new std::byte[16], 16);
    shared_buffer_view view(4, 6, buf);
    EXPECT_EQ(view.data(), buf->data() + 4);
    EXPECT_EQ(view.size(), 6u);
    EXPECT_FALSE(view.empty());
}

TEST(simple_shared_buffer_view, ShouldConstructConstViewFromNonConstView)
{
    auto buf = std::make_shared<buffer>(new std::byte[4], 4);
    shared_buffer_view mutable_view(buf);
    const_shared_buffer_view const_view(mutable_view);
    EXPECT_EQ(const_view.data(), buf->data());
    EXPECT_EQ(const_view.size(), 4u);
}

TEST(simple_shared_buffer_view, ShouldCopyConstruct)
{
    auto buf = std::make_shared<buffer>(new std::byte[4], 4);
    shared_buffer_view view1(buf);
    shared_buffer_view view2(view1);
    EXPECT_EQ(view1.data(), view2.data());
    EXPECT_EQ(view1.size(), view2.size());
    EXPECT_EQ(view1.get_shared_buffer(), view2.get_shared_buffer());
}

TEST(simple_shared_buffer_view, ShouldCopyAssign)
{
    auto buf = std::make_shared<buffer>(new std::byte[4], 4);
    shared_buffer_view view1(buf);
    shared_buffer_view view2(std::make_shared<buffer>(new std::byte[2], 2));
    view2 = view1;
    EXPECT_EQ(view1.data(), view2.data());
    EXPECT_EQ(view1.size(), view2.size());
}

TEST(simple_shared_buffer_view, ShouldView_ChangeOffsetAndSize)
{
    auto buf = std::make_shared<buffer>(new std::byte[10], 10);
    shared_buffer_view view(buf);
    view.view(2, 5);
    EXPECT_EQ(view.data(), buf->data() + 2);
    EXPECT_EQ(view.size(), 5u);
}

TEST(simple_shared_buffer_view, ShouldBeEmpty_WhenSizeZero)
{
    auto buf = std::make_shared<buffer>(new std::byte[4], 4);
    shared_buffer_view view(0, 0, buf);
    EXPECT_TRUE(view.empty());
    EXPECT_EQ(view.size(), 0u);
}

TEST(simple_shared_buffer_view, ShouldShareBuffer_AcrossViews)
{
    auto buf = std::make_shared<buffer>(new std::byte[4], 4);
    shared_buffer_view view1(buf);
    shared_buffer_view view2(buf);
    EXPECT_EQ(view1.get_shared_buffer(), view2.get_shared_buffer());
    // use_count: buf + view1.m_buffer + view2.m_buffer + temporary from get_shared_buffer()
    EXPECT_GE(view1.get_shared_buffer().use_count(), 3u);
}
