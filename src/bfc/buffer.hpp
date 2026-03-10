#ifndef __BFC_BUFFER_HPP__
#define __BFC_BUFFER_HPP__

#include <cstddef>     // std::byte, size_t
#include <type_traits> // std::is_nothrow_invocable_v
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

using buffer = simple_buffer<std::byte>;
using const_buffer = simple_buffer<const std::byte>;
using buffer_view = simple_buffer_view<std::byte>;
using const_buffer_view = simple_buffer_view<const std::byte>;

} // namespace bfc

#endif // __BFC_BUFFER_HPP__
