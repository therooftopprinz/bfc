#pragma once
// bfc's own tests: std::function callbacks on an epoll reactor, and an
// allocator type that counts its calls.
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>

#define BFC_CONFIG_ID "bfc-tests"
#define BFC_CONFIG_REACTOR_EPOLL 1

namespace bfc::config
{
using reactor_cb = std::function<void()>;
using queue_cb = std::function<void()>;
using timer_cb = std::function<void()>;

inline std::atomic<std::size_t> allocations{0};
inline std::atomic<std::size_t> deallocations{0};

template <typename T>
struct counting_allocator
{
    using value_type = T;
    counting_allocator() noexcept = default;
    template <typename U>
    counting_allocator(const counting_allocator<U> &) noexcept
    {
    }
    T *allocate(std::size_t n)
    {
        allocations.fetch_add(1);
        return std::allocator<T>().allocate(n);
    }
    void deallocate(T *p, std::size_t n) noexcept
    {
        deallocations.fetch_add(1);
        std::allocator<T>().deallocate(p, n);
    }
    template <typename U>
    bool operator==(const counting_allocator<U> &) const noexcept
    {
        return true;
    }
    template <typename U>
    bool operator!=(const counting_allocator<U> &) const noexcept
    {
        return false;
    }
};

template <typename T>
using allocator = counting_allocator<T>;
} // namespace bfc::config
