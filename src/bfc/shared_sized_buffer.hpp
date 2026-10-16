#ifndef __BFC_SHARED_SIZED_BUFFER_HPP__
#define __BFC_SHARED_SIZED_BUFFER_HPP__

#include <cstddef>    // std::byte, size_t
#include <cstdint>    // uint8_t
#include <cstring>    // std::memcpy
#include <functional> // std::function
#include <memory>     // std::shared_ptr

#include <bfc/buffer.hpp>

namespace bfc
{

// @brief Copyable, refcounted view over shared storage with a separate
// logical size (like bfc::sized_buffer, but shared). Several views may
// reference the same bytes at different offsets; the storage is released
// when the last view goes away.
class shared_sized_buffer
{
public:
    struct storage
    {
        storage() = default;
        storage(const storage&) = delete;
        storage& operator=(const storage&) = delete;

        ~storage()
        {
            if (release && bytes)
            {
                release(bytes);
            }
        }

        std::byte* bytes = nullptr;
        size_t capacity = 0;
        std::function<void(std::byte*)> release;
    };

    shared_sized_buffer() = default;
    shared_sized_buffer(const shared_sized_buffer&) = default;
    shared_sized_buffer& operator=(const shared_sized_buffer&) = default;

    shared_sized_buffer(shared_sized_buffer&& p_other) noexcept
        : m_storage(std::move(p_other.m_storage))
        , m_offset(p_other.m_offset)
        , m_size(p_other.m_size)
    {
        p_other.m_offset = 0;
        p_other.m_size = 0;
    }

    shared_sized_buffer& operator=(shared_sized_buffer&& p_other) noexcept
    {
        if (this != &p_other)
        {
            m_storage = std::move(p_other.m_storage);
            m_offset = p_other.m_offset;
            m_size = p_other.m_size;
            p_other.m_offset = 0;
            p_other.m_size = 0;
        }
        return *this;
    }

    // @brief Allocate p_size bytes and copy p_data into them.
    static shared_sized_buffer copy_from(const void* p_data, size_t p_size)
    {
        shared_sized_buffer out;
        if (!p_data || !p_size)
        {
            return out;
        }
        out.resize(p_size);
        std::memcpy(out.data(), p_data, p_size);
        return out;
    }

    // @brief Allocate p_capacity bytes; size() stays 0.
    static shared_sized_buffer allocate(size_t p_capacity)
    {
        shared_sized_buffer out;
        if (!p_capacity)
        {
            return out;
        }
        auto st = std::make_shared<storage>();
        st->bytes = new std::byte[p_capacity];
        st->capacity = p_capacity;
        st->release = [](std::byte* p) { delete[] p; };
        out.m_storage = std::move(st);
        return out;
    }

    // @brief Take ownership of p_data; p_release runs when the last view is
    // gone. On invalid arguments p_data is released immediately and an empty
    // buffer is returned.
    static shared_sized_buffer adopt(std::byte* p_data, size_t p_capacity, size_t p_size,
        std::function<void(std::byte*)> p_release)
    {
        shared_sized_buffer out;
        if (!p_data || !p_capacity || p_size > p_capacity)
        {
            if (p_release && p_data)
            {
                p_release(p_data);
            }
            return out;
        }
        auto st = std::make_shared<storage>();
        st->bytes = p_data;
        st->capacity = p_capacity;
        st->release = std::move(p_release);
        out.m_storage = std::move(st);
        out.m_size = p_size;
        return out;
    }

    size_t size() const noexcept
    {
        return m_size;
    }

    size_t offset() const noexcept
    {
        return m_offset;
    }

    size_t capacity() const noexcept
    {
        if (!m_storage)
        {
            return 0;
        }
        return m_storage->capacity - m_offset;
    }

    bool empty() const noexcept
    {
        return !m_storage || !m_size;
    }

    std::byte* data() const noexcept
    {
        if (!m_storage)
        {
            return nullptr;
        }
        return m_storage->bytes + m_offset;
    }

    uint8_t* u8() const noexcept
    {
        return reinterpret_cast<uint8_t*>(data());
    }

    const_buffer_view view() const noexcept
    {
        return const_buffer_view(data(), size());
    }

    std::shared_ptr<storage> get_storage() const noexcept
    {
        return m_storage;
    }

    long use_count() const noexcept
    {
        return m_storage ? m_storage.use_count() : 0;
    }

    void clear() noexcept
    {
        m_storage.reset();
        m_offset = 0;
        m_size = 0;
    }

    // @brief Ensure at least p_capacity bytes; reallocates (detaching from
    // other views) when growing. No-op on subviews (offset() != 0).
    void reserve(size_t p_capacity)
    {
        if (m_offset || p_capacity <= capacity())
        {
            return;
        }
        auto next = allocate(p_capacity);
        if (!empty())
        {
            std::memcpy(next.data(), data(), m_size);
            next.m_size = m_size;
        }
        *this = std::move(next);
    }

    // @brief Set the logical size, growing storage if needed. No-op on
    // subviews (offset() != 0).
    void resize(size_t p_size)
    {
        if (m_offset)
        {
            return;
        }
        if (p_size > capacity())
        {
            reserve(p_size);
        }
        m_size = p_size;
    }

    // @brief View of p_size bytes at p_offset relative to this view; empty
    // when out of range.
    shared_sized_buffer subview(size_t p_offset, size_t p_size) const noexcept
    {
        shared_sized_buffer out;
        if (!m_storage || p_offset > m_size || p_size > m_size - p_offset)
        {
            return out;
        }
        out.m_storage = m_storage;
        out.m_offset = m_offset + p_offset;
        out.m_size = p_size;
        return out;
    }

private:
    std::shared_ptr<storage> m_storage;
    size_t m_offset = 0;
    size_t m_size = 0;
};

} // namespace bfc

#endif // __BFC_SHARED_SIZED_BUFFER_HPP__
