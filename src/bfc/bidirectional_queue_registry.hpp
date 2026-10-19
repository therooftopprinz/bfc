#ifndef __BFC_BIDIRECTIONAL_QUEUE_REGISTRY_HPP__
#define __BFC_BIDIRECTIONAL_QUEUE_REGISTRY_HPP__

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <bfc/bidirectional_queue.hpp>

namespace bfc
{

enum class bidirectional_queue_role
{
    a,
    b,
};

template <typename T>
struct bidirectional_queue_endpoint_view
{
    T* outbound;
    T* inbound;
};

// In-process named bidirectional links for colocated peers (e.g. vstreamer role A,
// winject-l3 role B). Queues are unbounded; SPSC per outbound leg is by convention.
// reactive_event_queue::push does not wake a reactor — apps call wake after push.
template <typename T>
class bidirectional_queue_registry
{
public:
    static bidirectional_queue_registry& instance()
    {
        static bidirectional_queue_registry registry;
        return registry;
    }

    std::shared_ptr<bidirectional_queue<T>> get_or_create(std::string_view name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return entry_for(name).link;
    }

    // Registers at most one endpoint per (name, role). Returns false if role taken.
    bool endpoint_for(std::string_view name,
                      bidirectional_queue_role role,
                      void* endpoint,
                      bidirectional_queue_endpoint_view<T>* out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& slot = entry_for(name);
        void*& role_slot = role_endpoint(role, slot);
        if (role_slot != nullptr)
        {
            return false;
        }
        role_slot = endpoint;
        if (out != nullptr)
        {
            *out = view_for(role, *slot.link);
        }
        return true;
    }

private:
    struct entry_s
    {
        std::shared_ptr<bidirectional_queue<T>> link =
            std::make_shared<bidirectional_queue<T>>();
        void* a_endpoint = nullptr;
        void* b_endpoint = nullptr;
    };

    entry_s& entry_for(std::string_view name)
    {
        auto it = m_entries.find(std::string(name));
        if (it == m_entries.end())
        {
            it = m_entries.emplace(std::string(name), entry_s{}).first;
        }
        return it->second;
    }

    static void*& role_endpoint(bidirectional_queue_role role, entry_s& entry)
    {
        return role == bidirectional_queue_role::a ? entry.a_endpoint
                                                   : entry.b_endpoint;
    }

    static bidirectional_queue_endpoint_view<T> view_for(
        bidirectional_queue_role role,
        bidirectional_queue<T>& link)
    {
        if (role == bidirectional_queue_role::a)
        {
            return {&link.a, &link.b};
        }
        return {&link.b, &link.a};
    }

    std::mutex m_mutex;
    std::unordered_map<std::string, entry_s> m_entries;
};

}  // namespace bfc

#endif  // __BFC_BIDIRECTIONAL_QUEUE_REGISTRY_HPP__
