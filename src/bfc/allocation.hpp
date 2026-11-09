#ifndef __BFC_ALLOCATION_HPP__
#define __BFC_ALLOCATION_HPP__

// Allocation helpers over the project's bfc::config::allocator<T>. bfc defines
// no allocator of its own: it rebinds the config's allocator type for the
// bytes, control nodes and queue slots it allocates (sized_buffer,
// shared_sized_buffer, memory_pool, the lossy queue's slots). Allocation
// failure is whatever the allocator does (std::allocator throws bad_alloc).

#include <bfc/config.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace bfc
{

namespace detail
{
template <typename T>
using config_alloc_traits = std::allocator_traits<config::allocator<T>>;

template <std::size_t align>
struct alignas(align) aligned_block
{
    std::byte bytes[align];
};
} // namespace detail

// n > 0 bytes, suitably aligned for std::byte.
inline std::byte *bytes_allocate(std::size_t n)
{
    config::allocator<std::byte> a;
    return detail::config_alloc_traits<std::byte>::allocate(a, n);
}

inline void bytes_deallocate(const void *p, std::size_t n) noexcept
{
    config::allocator<std::byte> a;
    detail::config_alloc_traits<std::byte>::deallocate(a, static_cast<std::byte *>(const_cast<void *>(p)), n);
}

// n bytes aligned to align (a power of two), as whole aligned blocks; the
// config allocator receives an over-aligned element type and must honour it.
template <std::size_t align>
std::byte *aligned_bytes_allocate(std::size_t n)
{
    using block = detail::aligned_block<align>;
    typename detail::config_alloc_traits<std::byte>::template rebind_alloc<block> a;
    const std::size_t blocks = n ? (n + align - 1) / align : 1; // never a zero-size request
    auto *p = reinterpret_cast<std::byte *>(std::allocator_traits<decltype(a)>::allocate(a, blocks));
    // config::allocator<T> must honour alignof(T); catch one that does not.
    assert(reinterpret_cast<std::uintptr_t>(p) % align == 0);
    return p;
}

template <std::size_t align>
void aligned_bytes_deallocate(const void *p, std::size_t n) noexcept
{
    using block = detail::aligned_block<align>;
    typename detail::config_alloc_traits<std::byte>::template rebind_alloc<block> a;
    const std::size_t blocks = n ? (n + align - 1) / align : 1;
    std::allocator_traits<decltype(a)>::deallocate(a, static_cast<block *>(const_cast<void *>(p)), blocks);
}

// Constructs a T in memory from the config allocator.
template <typename T, typename... args_t>
T *alloc_new(args_t &&...args)
{
    config::allocator<T> a;
    T *p = detail::config_alloc_traits<T>::allocate(a, 1);
    try
    {
        detail::config_alloc_traits<T>::construct(a, p, std::forward<args_t>(args)...);
    }
    catch (...)
    {
        detail::config_alloc_traits<T>::deallocate(a, p, 1);
        throw;
    }
    return p;
}

template <typename T>
void alloc_delete(T *p) noexcept
{
    if (p)
    {
        config::allocator<T> a;
        detail::config_alloc_traits<T>::destroy(a, p);
        detail::config_alloc_traits<T>::deallocate(a, p, 1);
    }
}

// unique_ptr over the config allocator: the deleter returns the object to it.
// Unlike the containers this is a different type from std::unique_ptr<T> even
// with the default allocator (the deleter differs), so use it for owning
// members, not in public signatures.
template <typename T>
struct alloc_deleter
{
    void operator()(T *p) const noexcept
    {
        alloc_delete(p);
    }
};

template <typename T>
using unique_ptr = std::unique_ptr<T, alloc_deleter<T>>;

template <typename T, typename... args_t>
unique_ptr<T> make_unique(args_t &&...args)
{
    return unique_ptr<T>(alloc_new<T>(std::forward<args_t>(args)...));
}

} // namespace bfc

#endif // __BFC_ALLOCATION_HPP__
