#ifndef __BFC_DEFAULT_REACTOR_HPP__
#define __BFC_DEFAULT_REACTOR_HPP__

// epoll_reactor where epoll exists, poll_reactor elsewhere.
// TARGET_LINUX / TARGET_POSIX force the choice.
#if defined(TARGET_LINUX)
#define BFC_DEFAULT_REACTOR_EPOLL 1
#elif defined(TARGET_POSIX)
#elif defined(__has_include)
#if __has_include(<sys/epoll.h>)
#define BFC_DEFAULT_REACTOR_EPOLL 1
#endif
#elif defined(__linux__)
#define BFC_DEFAULT_REACTOR_EPOLL 1
#endif

#if defined(BFC_DEFAULT_REACTOR_EPOLL)
#include <bfc/epoll_reactor.hpp>
#else
#include <bfc/poll_reactor.hpp>
#endif

#include <bfc/object_registry.hpp>

namespace bfc
{

#if defined(BFC_DEFAULT_REACTOR_EPOLL)
template <typename cb_t = light_function<void()>>
using default_reactor = epoll_reactor<cb_t>;
#else
template <typename cb_t = light_function<void()>>
using default_reactor = poll_reactor<cb_t>;
#endif

using default_reactor_tag = object_tag<default_reactor<>>;
using default_reactor_registry = object_registry<default_reactor_tag>;

} // namespace bfc

#endif // __BFC_DEFAULT_REACTOR_HPP__
