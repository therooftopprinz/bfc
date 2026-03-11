#ifndef __BFC_MEMORYPOOL_HPP__
#define __BFC_MEMORYPOOL_HPP__

#include <mutex>
#include <array>
#include <cmath>
#include <vector>
#include <cstddef>

#include <bfc/buffer.hpp>

namespace bfc
{

template <size_t ALIGNMENT=alignof(std::max_align_t)>
class sized_memory_pool
{
public:
    sized_memory_pool(size_t p_size)
        : m_size(p_size)
    {}

    ~sized_memory_pool()
    {
        for (auto i : m_allocations)
        {
            operator delete[](i, std::align_val_t {ALIGNMENT});
        }
    }

    buffer allocate()
    {
        std::byte* rv = allocate_raw();
        return buffer(rv, m_size, [this](const void* p_ptr){free(p_ptr);});
    }

    void free(const void* p_ptr)
    {
        std::unique_lock<std::mutex> lg(m_alloc_mtx);
        m_allocations.emplace_back((std::byte*)(p_ptr));
    }

    size_t size() const
    {
        return m_size;
    }

private:
    std::byte* allocate_raw()
    {
        std::unique_lock<std::mutex> lg(m_alloc_mtx);
        if (!m_allocations.empty())
        {
            std::byte* rv = m_allocations.back();
            m_allocations.pop_back();
            return rv;
        }
        lg.unlock();
        return static_cast<std::byte*>(
            operator new[](m_size, std::align_val_t{ALIGNMENT}));
    }

    const size_t m_size;
    std::vector<std::byte*> m_allocations;
    std::mutex m_alloc_mtx;
};

template <size_t ALIGNMENT = alignof(std::max_align_t)>
class log2_memory_pool
{
public:
    buffer allocate(size_t p_size)
    {
        if (p_size<=8)
        {
            throw std::bad_alloc();
        }
        int index = std::ceil(std::log2(p_size))-4;
        return m_pools.at(index).allocate();
    }

    void free(const std::byte* p_alloc, size_t p_size)
    {
        if (p_size<=8)
        {
            throw std::bad_alloc();
        }
        int index = std::ceil(std::log2(p_size))-4;
        return m_pools.at(index).free(p_alloc);
    }

private:
    using pool_t = sized_memory_pool<ALIGNMENT>;
    std::array<pool_t,11> m_pools = {
        pool_t(16),
        pool_t(32),
        pool_t(64),
        pool_t(128),
        pool_t(256),
        pool_t(512),
        pool_t(1024),
        pool_t(2048),
        pool_t(4096),
        pool_t(8192),
        pool_t(16384)
    };
};

} // namespace bfc

#endif // __BFC_MEMORYPOOL_HPP__
