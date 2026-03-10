#ifndef __BFC_FUNCTION_HPP__
#define __BFC_FUNCTION_HPP__

#include <type_traits>
#include <functional>
#include <cstddef>
#include <utility>
#include <new>

namespace bfc
{

template <size_t N, typename return_t, typename... args_t>
class function
{
public:
    function() = default;

    function(std::nullptr_t)
    {
        clear();
    }

    function(const function &p_other)
    {
        if (p_other)
        {
            p_other.m_copier(static_cast<void *>(m_object), static_cast<const void *>(p_other.m_object));
            copy_meta_from(p_other);
        }
        else
        {
            clear();
        }
    }

    function(function &&p_other)
    {
        if (p_other)
        {
            p_other.m_mover(static_cast<void *>(m_object), static_cast<void *>(p_other.m_object));
            copy_meta_from(p_other);
            p_other.reset();
        }
        else
        {
            clear();
        }
    }

    template <typename callable_t, std::enable_if_t<!std::is_same_v<std::remove_reference_t<callable_t>, function>> *p = nullptr>
    function(callable_t &&p_obj)
    {
        set(std::forward<callable_t>(p_obj));
    }

    function &operator=(const function &p_other)
    {
        if (this != &p_other)
        {
            function tmp(p_other);
            swap(tmp);
        }
        return *this;
    }

    function &operator=(function &&p_other)
    {
        if (this != &p_other)
        {
            reset();
            if (p_other)
            {
                p_other.m_mover(static_cast<void *>(m_object), static_cast<void *>(p_other.m_object));
                copy_meta_from(p_other);
                p_other.reset();
            }
            else
            {
                clear();
            }
        }
        return *this;
    }

    template <typename callable_t, std::enable_if_t<!std::is_same_v<std::remove_reference_t<callable_t>, function>> *p = nullptr>
    function &operator=(callable_t &&p_obj)
    {
        reset();
        set(std::forward<callable_t>(p_obj));
        return *this;
    }

    function &operator=(std::nullptr_t)
    {
        reset();
        return *this;
    }

    ~function()
    {
        if (m_fn)
        {
            m_destroyer(m_object);
        }
    }

    explicit operator bool() const
    {
        return m_fn;
    }

    void reset()
    {
        if (m_fn)
        {
            m_destroyer(m_object);
        }

        clear();
    }

    return_t operator()(args_t... pArgs) const
    {
        if (m_fn)
        {
            return m_fn(const_cast<void *>(static_cast<const void *>(m_object)), std::forward<args_t>(pArgs)...);
        }
        else
        {
            throw std::bad_function_call();
        }
    }

    void swap(function &p_other) noexcept
    {
        if (this == &p_other)
        {
            return;
        }

        using std::swap;
        swap(m_fn, p_other.m_fn);
        swap(m_destroyer, p_other.m_destroyer);
        swap(m_copier, p_other.m_copier);
        swap(m_mover, p_other.m_mover);

        for (size_t i = 0; i < N; ++i)
        {
            swap(m_object[i], p_other.m_object[i]);
        }
    }

    friend void swap(function &a, function &b) noexcept
    {
        a.swap(b);
    }

private:
    template <typename callable_t, std::enable_if_t<!std::is_same_v<std::remove_reference_t<callable_t>, function>> *p = nullptr>
    void set(callable_t &&p_obj)
    {
        using callable_tType = std::decay_t<callable_t>;
        static_assert(N >= sizeof(callable_tType), "bfc::function storage too small for callable");
        static_assert(alignof(std::max_align_t) % alignof(callable_tType) == 0,
                      "bfc::function storage not properly aligned for callable");

        new (static_cast<void *>(m_object)) callable_tType(std::forward<callable_t>(p_obj));
        m_destroyer = [](void *p_obj) {
            static_cast<callable_tType *>(p_obj)->~callable_tType();
        };

        m_copier = [](void *p_obj, const void *p_other) {
            new (p_obj) callable_tType(*static_cast<const callable_tType *>(p_other));
        };

        m_mover = [](void *p_obj, void *p_other) {
            new (p_obj) callable_tType(std::move(*static_cast<callable_tType *>(p_other)));
        };

        m_fn = [](void *p_obj, args_t... pArgs) -> return_t {
            return (*static_cast<callable_tType *>(p_obj))(std::forward<args_t>(pArgs)...);
        };
    }

    void copy_meta_from(const function &p_other)
    {
        m_fn = p_other.m_fn;
        m_destroyer = p_other.m_destroyer;
        m_copier = p_other.m_copier;
        m_mover = p_other.m_mover;
    }

    void set(const std::nullptr_t)
    {
        clear();
    }

    void clear()
    {
        m_fn = nullptr;
    }

    alignas(std::max_align_t) std::byte m_object[N]{};
    return_t (*m_fn)(void *, args_t...) = nullptr;
    void (*m_destroyer)(void *) = nullptr;
    void (*m_copier)(void *, const void *) = nullptr;
    void (*m_mover)(void *, void *) = nullptr;
};

template <size_t N, typename T>
struct function_type_helper;
template <size_t N, typename return_t, typename... args_t>
struct function_type_helper<N, return_t(args_t...)>
{
    using type = function<N, return_t, args_t...>;
};

template <typename function_t>
using ulight_function = typename function_type_helper<8, function_t>::type;
template <typename function_t>
using light_function = typename function_type_helper<24, function_t>::type;
template <typename function_t>
using big_function = typename function_type_helper<32, function_t>::type;

} // namespace bfc

#endif // __BFC_FUNCTION_HPP__