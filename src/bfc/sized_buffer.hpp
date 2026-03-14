#ifndef __BFC_SIZE_BUFFER_HPP__
#define __BFC_SIZE_BUFFER_HPP__

#include <cstddef>  // std::byte, size_t
#include <cstring>  // std::memcpy

#include <bfc/buffer.hpp>

namespace bfc
{

// @brief Wrapper around bfc::buffer that tracks logical size separately
// from the underlying storage capacity. This is useful when building
// buffers incrementally where the final payload size is not known
// upfront.
class sized_buffer
{
public:
    sized_buffer() = default;

    explicit sized_buffer(size_t initial_size)
    {
        resize(initial_size);
    }

    explicit sized_buffer(bfc::buffer&& buffer, size_t size)
    {
        m_storage = std::move(buffer);
        m_size = size;
    }

    sized_buffer(const sized_buffer&) = delete;
    sized_buffer& operator=(const sized_buffer&) = delete;

    sized_buffer(sized_buffer&&) noexcept = default;
    sized_buffer& operator=(sized_buffer&&) noexcept = default;

    size_t size() const noexcept
    {
        return m_size;
    }

    size_t capacity() const noexcept
    {
        return m_storage.size();
    }

    bool empty() const noexcept
    {
        return m_size == 0;
    }

    std::byte* data() const noexcept
    {
        return m_storage.data();
    }

    void clear() noexcept
    {
        m_storage.reset();
        m_size = 0;
    }

    // @brief Ensure at least new_capacity bytes of storage are available.
    // Does not change the logical size().
    void reserve(size_t new_capacity)
    {
        if (new_capacity <= capacity())
        {
            return;
        }

        auto* data = new std::byte[new_capacity];

        if (m_storage.data() != nullptr && m_size > 0)
        {
            std::memcpy(data, m_storage.data(), m_size);
        }

        m_storage = bfc::buffer(data, new_capacity);
    }

    // @brief Change logical size to new_size, growing capacity if needed.
    void resize(size_t new_size)
    {
        if (new_size > capacity())
        {
            reserve(new_size);
        }
        m_size = new_size;
    }

    // @brief Convert to a bfc::buffer owning exactly size() bytes.
    // When capacity() == size(), this simply moves out the underlying storage.
    // Otherwise, it allocates a right-sized buffer and copies the payload.
    bfc::buffer to_buffer() &&
    {
        if (m_size == 0)
        {
            clear();
            return {};
        }

        if (m_size == m_storage.size())
        {
            m_size = 0;
            return std::move(m_storage);
        }

        const size_t n = m_size;
        auto* data = new std::byte[n];
        if (m_storage.data() != nullptr)
        {
            std::memcpy(data, m_storage.data(), n);
        }

        clear();
        return bfc::buffer(data, n);
    }

private:
    size_t     m_size    = 0;
    bfc::buffer m_storage{};
};

} // namespace bfc

#endif // __BFC_SIZE_BUFFER_HPP__

