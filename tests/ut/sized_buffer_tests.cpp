#include <gtest/gtest.h>

#include <bfc/sized_buffer.hpp>

using namespace bfc;

TEST(sized_buffer, DefaultConstruct_Empty)
{
    sized_buffer sb;
    EXPECT_EQ(sb.size(), 0u);
    EXPECT_EQ(sb.capacity(), 0u);
    EXPECT_TRUE(sb.empty());
    EXPECT_EQ(sb.data(), nullptr);
}

TEST(sized_buffer, ConstructWithInitialSize)
{
    sized_buffer sb(16);
    EXPECT_EQ(sb.size(), 16u);
    EXPECT_EQ(sb.capacity(), 16u);
    EXPECT_FALSE(sb.empty());
    EXPECT_NE(sb.data(), nullptr);
}

TEST(sized_buffer, ConstructFromBufferAndSize)
{
    auto* data = new std::byte[8];
    bfc::buffer buf(data, 8);
    sized_buffer sb(std::move(buf), 4);
    EXPECT_EQ(sb.size(), 4u);
    EXPECT_EQ(sb.capacity(), 8u);
    EXPECT_NE(sb.data(), nullptr);
}

TEST(sized_buffer, Reserve_GrowsCapacity_DoesNotChangeSize)
{
    sized_buffer sb(4);
    sb.reserve(32);
    EXPECT_EQ(sb.size(), 4u);
    EXPECT_EQ(sb.capacity(), 32u);
}

TEST(sized_buffer, Reserve_NoOp_WhenNewCapacitySmallerOrEqual)
{
    sized_buffer sb(16);
    sb.reserve(8);
    EXPECT_EQ(sb.capacity(), 16u);
    sb.reserve(16);
    EXPECT_EQ(sb.capacity(), 16u);
}

TEST(sized_buffer, Resize_GrowsLogicalSize)
{
    sized_buffer sb(4);
    sb.resize(8);
    EXPECT_EQ(sb.size(), 8u);
    EXPECT_GE(sb.capacity(), 8u);
}

TEST(sized_buffer, Resize_ShrinksLogicalSize_DoesNotShrinkCapacity)
{
    sized_buffer sb(16);
    sb.resize(4);
    EXPECT_EQ(sb.size(), 4u);
    EXPECT_EQ(sb.capacity(), 16u);
}

TEST(sized_buffer, Clear_ResetsSizeAndStorage)
{
    sized_buffer sb(8);
    sb.clear();
    EXPECT_EQ(sb.size(), 0u);
    EXPECT_EQ(sb.capacity(), 0u);
    EXPECT_TRUE(sb.empty());
}

TEST(sized_buffer, ToBuffer_WhenSizeEqualsCapacity_MovesStorage)
{
    sized_buffer sb(4);
    bfc::buffer out = std::move(sb).to_buffer();
    EXPECT_EQ(out.size(), 4u);
    EXPECT_NE(out.data(), nullptr);
    EXPECT_TRUE(sb.empty());
    EXPECT_EQ(sb.capacity(), 0u);
}

TEST(sized_buffer, ToBuffer_WhenSizeLessThanCapacity_CopiesPayload)
{
    sized_buffer sb(8);
    sb.resize(3);
    bfc::buffer out = std::move(sb).to_buffer();
    EXPECT_EQ(out.size(), 3u);
    EXPECT_NE(out.data(), nullptr);
}

TEST(sized_buffer, ToBuffer_WhenEmpty_ReturnsEmptyBuffer)
{
    sized_buffer sb;
    bfc::buffer out = std::move(sb).to_buffer();
    EXPECT_EQ(out.size(), 0u);
    EXPECT_EQ(out.data(), nullptr);
}

TEST(sized_buffer, MoveConstruct)
{
    sized_buffer sb1(8);
    sized_buffer sb2(std::move(sb1));
    EXPECT_EQ(sb2.size(), 8u);
    EXPECT_EQ(sb2.capacity(), 8u);
    // moved-from state is unspecified; only assert on the new object
}

TEST(sized_buffer, MoveAssign)
{
    sized_buffer sb1(8);
    sized_buffer sb2;
    sb2 = std::move(sb1);
    EXPECT_EQ(sb2.size(), 8u);
    EXPECT_EQ(sb2.capacity(), 8u);
    // moved-from state is unspecified; only assert on the new object
}

TEST(sized_buffer, Reserve_PreservesExistingData)
{
    sized_buffer sb(4);
    std::byte* first = sb.data();
    for (size_t i = 0; i < 4; ++i)
        first[i] = static_cast<std::byte>(i);
    sb.reserve(16);
    for (size_t i = 0; i < 4; ++i)
        EXPECT_EQ(sb.data()[i], static_cast<std::byte>(i));
}
