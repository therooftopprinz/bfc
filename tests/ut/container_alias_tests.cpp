#include <gtest/gtest.h>

#include <bfc/allocation.hpp>
#include <bfc/config.hpp>
#include <bfc/containers.hpp>
#include <bfc/default_reactor.hpp>
#include <bfc/cv_reactor.hpp>
#include <bfc/object_registry.hpp>
#include <bfc/timer.hpp>

#include <type_traits>

#ifdef BFC_TEST_COUNTING_ALLOCATOR
namespace
{
struct snapshot
{
    std::size_t a = bfc::config::thread_allocations;
    std::size_t allocs() const { return bfc::config::thread_allocations - a; }
};
struct widget
{
};
} // namespace

TEST(ContainerAliasTest, aliases_use_the_config_allocator)
{
    snapshot s;
    bfc::vector<int> v;
    v.push_back(1);
    bfc::list<int> l;
    l.push_back(1);
    bfc::map<int, int> m;
    m.emplace(1, 1);
    bfc::unordered_map<int, int> u;
    u.emplace(1, 1);
    bfc::string str(64, 'x');
    EXPECT_GE(s.allocs(), 5u);
}

TEST(ContainerAliasTest, object_registry_allocates_through_the_config_allocator)
{
    auto& reg = bfc::object_registry<bfc::object_tag<widget>>::get_instance();
    snapshot s;
    EXPECT_TRUE(reg.add("a-fairly-long-name-that-does-not-fit-the-small-string-buffer", std::make_shared<widget>()));
    EXPECT_GE(s.allocs(), 2u); // the map node and the long key
    EXPECT_NE(nullptr, reg.get("a-fairly-long-name-that-does-not-fit-the-small-string-buffer"));
    EXPECT_TRUE(reg.rem("a-fairly-long-name-that-does-not-fit-the-small-string-buffer"));
}

TEST(ContainerAliasTest, timer_map_allocates_through_the_config_allocator)
{
    bfc::timer<> t;
    snapshot s;
    t.wait_ms(1000, [] {});
    EXPECT_GE(s.allocs(), 1u);
}
#else
TEST(ContainerAliasTest, default_allocator_leaves_the_std_types_unchanged)
{
    static_assert(std::is_same_v<bfc::vector<int>, std::vector<int>>);
    static_assert(std::is_same_v<bfc::list<int>, std::list<int>>);
    static_assert(std::is_same_v<bfc::map<int, int>, std::map<int, int>>);
    static_assert(std::is_same_v<bfc::unordered_map<int, int>, std::unordered_map<int, int>>);
    static_assert(std::is_same_v<bfc::string, std::string>);
    SUCCEED();
}
#endif

TEST(ContainerAliasTest, istringstream_and_unique_ptr_use_the_config_allocator)
{
    bfc::istringstream in(bfc::string("42 rest-of-line"));
    int v = 0;
    in >> v;
    EXPECT_EQ(42, v);

    struct node
    {
        int x = 7;
    };
#ifdef BFC_TEST_COUNTING_ALLOCATOR
    const auto before = bfc::config::thread_allocations;
    const auto before_free = bfc::config::thread_deallocations;
#endif
    {
        auto p = bfc::make_unique<node>();
        EXPECT_EQ(7, p->x);
#ifdef BFC_TEST_COUNTING_ALLOCATOR
        EXPECT_EQ(before + 1, bfc::config::thread_allocations);
#endif
    }
#ifdef BFC_TEST_COUNTING_ALLOCATOR
    EXPECT_EQ(before_free + 1, bfc::config::thread_deallocations); // the deleter returned it
#endif
}
