#ifndef __BFC_OBJECT_REGISTRY_HPP__
#define __BFC_OBJECT_REGISTRY_HPP__

#include <bfc/containers.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace bfc
{

template <typename T>
struct object_tag
{
    using type_t = T;
};

template <class tag_t>
class object_registry
{
public:
    using object_t = typename tag_t::type_t;

    static object_registry& get_instance()
    {
        static object_registry instance;
        return instance;
    }

    bool add(std::string_view name, std::shared_ptr<object_t> object)
    {
        if (!object)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        const bfc::string key(name.data(), name.size());
        if (m_objects.find(key) != m_objects.end())
        {
            return false;
        }
        m_objects.emplace(std::move(key), std::move(object));
        return true;
    }

    bool rem(std::string_view name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_objects.erase(bfc::string(name.data(), name.size())) != 0;
    }

    std::shared_ptr<object_t> get(std::string_view name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_objects.find(bfc::string(name.data(), name.size()));
        if (it == m_objects.end())
        {
            return {};
        }
        return it->second;
    }

    object_registry(const object_registry&) = delete;
    object_registry& operator=(const object_registry&) = delete;
    object_registry(object_registry&&) = delete;
    object_registry& operator=(object_registry&&) = delete;

private:
    object_registry() = default;
    ~object_registry() = default;

    std::mutex m_mutex;
    bfc::unordered_map<bfc::string, std::shared_ptr<object_t>, bfc::string_hash> m_objects;
};

}  // namespace bfc

#endif  // __BFC_OBJECT_REGISTRY_HPP__
