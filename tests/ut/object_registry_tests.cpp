#include <gtest/gtest.h>

#include <bfc/cv_reactor.hpp>
#include <bfc/default_reactor.hpp>

using namespace bfc;

TEST(BfcObjectRegistry, AddGetAndDuplicateAddFails)
{
    auto& registry = object_registry<object_tag<int>>::get_instance();
    const char kName[] = "alpha";

    EXPECT_TRUE(registry.add(kName, std::make_shared<int>(7)));
    auto value = registry.get(kName);
    ASSERT_TRUE(value);
    EXPECT_EQ(*value, 7);

    EXPECT_FALSE(registry.add(kName, std::make_shared<int>(8)));
    value = registry.get(kName);
    EXPECT_EQ(*value, 7);

    EXPECT_TRUE(registry.rem(kName));
}

TEST(BfcObjectRegistry, RemRemovesEntry)
{
    auto& registry = object_registry<object_tag<int>>::get_instance();
    const char kName[] = "beta";

    EXPECT_TRUE(registry.add(kName, std::make_shared<int>(1)));
    EXPECT_TRUE(registry.rem(kName));
    EXPECT_FALSE(registry.get(kName));
    EXPECT_FALSE(registry.rem(kName));
}

TEST(BfcObjectRegistry, GetMissingReturnsEmpty)
{
    auto& registry = object_registry<object_tag<int>>::get_instance();
    EXPECT_FALSE(registry.get("missing-name"));
}

TEST(BfcObjectRegistry, NullObjectRejected)
{
    auto& registry = object_registry<object_tag<int>>::get_instance();
    std::shared_ptr<int> empty;
    EXPECT_FALSE(registry.add("null-add", empty));
}

TEST(BfcObjectRegistry, DefaultReactorRegistryAlias)
{
    auto reactor = std::make_shared<default_reactor<>>();
    auto& registry = default_reactor_registry::get_instance();
    EXPECT_TRUE(registry.add("default-main", reactor));
    EXPECT_EQ(registry.get("default-main"), reactor);
    EXPECT_TRUE(registry.rem("default-main"));
}

TEST(BfcObjectRegistry, TypedReactorRegistryAliases)
{
    auto reactor = std::make_shared<cv_reactor<>>();
    auto& cv_registry = cv_reactor_registry::get_instance();
    EXPECT_TRUE(cv_registry.add("main", reactor));
    EXPECT_EQ(cv_registry.get("main"), reactor);

    auto queue = std::make_shared<cv_reactor_event_queue_tag<>::type_t>();
    auto& queue_registry = cv_reactor_event_queue_registry<>::get_instance();
    EXPECT_TRUE(queue_registry.add("events", queue));
    EXPECT_EQ(queue_registry.get("events"), queue);

    EXPECT_TRUE(cv_registry.rem("main"));
    EXPECT_TRUE(queue_registry.rem("events"));
}
