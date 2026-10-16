#include <gtest/gtest.h>

#include <bfc/shared_sized_buffer.hpp>

using namespace bfc;

TEST(shared_sized_buffer, copy_from_and_subview_share_storage)
{
    const uint8_t raw[] = {0, 1, 2, 3, 4, 5, 6, 7};
    auto whole = shared_sized_buffer::copy_from(raw, sizeof(raw));
    EXPECT_EQ(whole.size(), sizeof(raw));
    EXPECT_FALSE(whole.empty());

    auto slice = whole.subview(2, 3);
    EXPECT_EQ(slice.size(), 3u);
    EXPECT_EQ(slice.u8()[0], 2);
    EXPECT_EQ(slice.u8()[2], 4);
    EXPECT_EQ(whole.get_storage(), slice.get_storage());
    EXPECT_EQ(whole.use_count(), 2);

    auto moved = std::move(slice);
    EXPECT_TRUE(slice.empty());
    EXPECT_EQ(moved.size(), 3u);
    EXPECT_EQ(moved.view().size(), 3u);
}

TEST(shared_sized_buffer, reserve_resize)
{
    shared_sized_buffer buf;
    buf.reserve(64);
    EXPECT_GE(buf.capacity(), 64u);
    buf.resize(8);
    EXPECT_EQ(buf.size(), 8u);
    std::memcpy(buf.data(), "abcdefgh", 8);

    auto moved = std::move(buf);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(moved.size(), 8u);

    moved.clear();
    moved.reserve(32);
    moved.resize(4);
    EXPECT_EQ(moved.capacity(), 32u);
}

TEST(shared_sized_buffer, subview_out_of_range_is_empty)
{
    const uint8_t raw[] = {1, 2, 3};
    auto whole = shared_sized_buffer::copy_from(raw, sizeof(raw));
    EXPECT_TRUE(whole.subview(4, 1).empty());
    EXPECT_TRUE(whole.subview(1, 3).empty());
}

TEST(shared_sized_buffer, adopt_releases_on_last_owner)
{
    int released = 0;
    auto* raw = new std::byte[16];
    {
        auto buf = shared_sized_buffer::adopt(raw, 16, 8, [&](std::byte* p){ ++released; delete[] p; });
        auto slice = buf.subview(2, 4);
        EXPECT_EQ(buf.use_count(), 2);
        EXPECT_EQ(buf.data(), raw);
        buf.clear();
        EXPECT_EQ(released, 0);
    }
    EXPECT_EQ(released, 1);
}
