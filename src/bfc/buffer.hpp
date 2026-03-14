#ifndef __BFC_BUFFER_HPP__
#define __BFC_BUFFER_HPP__

#include <cstddef>     // std::byte, size_t
#include <memory>     // std::shared_ptr
#include <type_traits> // std::is_nothrow_invocable_v, std::remove_cv_t
#include <utility>     // std::move

#include <bfc/function.hpp>

namespace bfc
{

template<typename T, typename D = light_function<void(const void*)>>
class simple_buffer
{
public:
    template <typename U>
    simple_buffer(U* p_data, size_t p_size, D p_deleter = [](const void* p_ptr){delete[] (const T*)p_ptr;})
        : m_size(p_size)
        , m_data(p_data)
        , m_deleter(std::move(p_deleter))
    {
        static_assert(sizeof(U)==1);
    }

    ~simple_buffer()
    {
        reset();
    }

    simple_buffer() = default;
    simple_buffer(const simple_buffer&) = delete;
    void operator=(const simple_buffer&) = delete;

    simple_buffer(simple_buffer&& p_other) noexcept
        : m_size(p_other.m_size)
        , m_data(p_other.m_data)
        , m_deleter(std::move(p_other.m_deleter))
    {
        clear(p_other);
    }

    simple_buffer& operator=(simple_buffer&& p_other) noexcept
    {
        if (this != &p_other)
        {
            reset();
            transfer(p_other);
        }
        return *this;
    }

    T* data() const
    {
        return m_data;
    }

    bool empty() const noexcept
    {
        return m_size == 0;
    }

    size_t size() const
    {
        return m_size;
    }

    void reset() noexcept
    {
        if (m_data)
        {
            m_deleter(static_cast<const void*>(m_data));
        }
        clear(*this);
    }

private:
    static void clear(simple_buffer& p_other) noexcept
    {
        p_other.m_data = nullptr;
        p_other.m_size = 0;
        p_other.m_deleter = D{};
    }

    void transfer(simple_buffer& p_other) noexcept
    {
        m_data = p_other.m_data;
        m_size = p_other.m_size;
        m_deleter = std::move(p_other.m_deleter);
        clear(p_other);
    }

    size_t m_size = 0;
    T* m_data = nullptr;
    D  m_deleter;

    static_assert(sizeof(T)==1);
    static_assert(std::is_invocable_v<D, const void*>);
};

template<typename T>
class simple_buffer_view
{
public:
    simple_buffer_view() = default;

    template<typename U>
    simple_buffer_view(U&& p_buffer)
        : m_size(p_buffer.size())
        , m_data(p_buffer.data())
    {}

    template <typename U>
    simple_buffer_view(U* data, size_t size)
        : m_size(size)
        , m_data(reinterpret_cast<T*>(data))
    {
        static_assert(sizeof(U)==1);
    }

    template<typename U>
    simple_buffer_view& operator=(U&& p_buffer)
    {
        m_size = p_buffer.size();
        m_data = p_buffer.data();
        return *this;
    }

    T* data() const
    {
        return m_data;
    }

    bool empty() const noexcept
    {
        return m_size == 0;
    }

    size_t size() const
    {
        return m_size;
    }

private:
    size_t m_size = 0;
    T* m_data = nullptr;

    static_assert(sizeof(T)==1);
};

template<typename T>
class simple_shared_buffer_view 
{
    using buffer_type = simple_buffer<std::remove_cv_t<T>>;

public:
    simple_shared_buffer_view() noexcept = delete;

    // @note UB if p_buffer is nullptr
    explicit simple_shared_buffer_view(std::shared_ptr<buffer_type> p_buffer) noexcept
        : m_buffer(std::move(p_buffer))
        , m_offset(0)
        , m_size(m_buffer->size())
    {}

    // @note UB if p_buffer is nullptr and if offset + size is greater than p_buffer->size()
    explicit simple_shared_buffer_view(size_t offset, size_t size, std::shared_ptr<buffer_type> p_buffer) noexcept
        : m_buffer(std::move(p_buffer))
        , m_offset(offset)
        , m_size(size)
    {}

    // @brief Construct const view from non-const view (shares the same buffer)
    template<typename U>
    simple_shared_buffer_view(const simple_shared_buffer_view<U>& p_other) noexcept
        : m_buffer(p_other.get_shared_buffer())
        , m_offset(p_other.m_offset)
        , m_size(p_other.m_size)
    {
        static_assert(std::is_same_v<T, const U>, "constructor only for const view from non-const view");
    }

    template <typename U>
    friend class simple_shared_buffer_view;

    simple_shared_buffer_view(const simple_shared_buffer_view& p_other)
        : m_buffer(p_other.m_buffer)
        , m_offset(p_other.m_offset)
        , m_size(p_other.m_size)
    {}

    simple_shared_buffer_view& operator=(const simple_shared_buffer_view& p_other)
    {
        m_buffer = p_other.m_buffer;
        m_offset = p_other.m_offset;
        m_size = p_other.m_size;
        return *this;
    }

    void view(size_t offset, size_t size) noexcept
    {
        m_offset = offset;
        m_size = size;
    }

    T* data() const noexcept
    {
        return m_buffer->data() + m_offset;
    }

    size_t size() const noexcept
    {
        return m_size;
    }

    bool empty() const noexcept
    {
        return size() == 0;
    }

    std::shared_ptr<buffer_type> get_shared_buffer() const noexcept
    {
        return m_buffer;
    }

private:
    std::shared_ptr<buffer_type> m_buffer;
    size_t m_offset = 0;
    size_t m_size = 0;
    static_assert(sizeof(T)==1);
};

using buffer            = simple_buffer<std::byte>;
using const_buffer      = simple_buffer<const std::byte>;
using buffer_view       = simple_buffer_view<std::byte>;
using const_buffer_view = simple_buffer_view<const std::byte>;

using shared_buffer            = std::shared_ptr<buffer>;
using shared_const_buffer      = std::shared_ptr<const_buffer>;
using shared_buffer_view       = simple_shared_buffer_view<std::byte>;
using const_shared_buffer_view = simple_shared_buffer_view<const std::byte>;
 
} // namespace bfc

#endif // __BFC_BUFFER_HPP__
