
#ifndef __BFC_DEFAULT_REACTOR_HPP__
#define __BFC_DEFAULT_REACTOR_HPP__


#if defined(TARGET_LINUX)
#include <bfc/epoll_reactor.hpp>
#elif defined(TARGET_POSIX)
#include <bfc/poll_reactor.hpp>
#elif defined(__linux__)
#include <bfc/epoll_reactor.hpp>
#else
#include <bfc/poll_reactor.hpp>
#endif

namespace bfc
{

#if defined(TARGET_LINUX)
template <typename cb_t = light_function<void()>>
using default_reactor = epoll_reactor<cb_t>;
#elif defined(TARGET_POSIX)
template <typename cb_t = light_function<void()>>
using default_reactor = poll_reactor<cb_t>;
#elif defined(__linux__)
template <typename cb_t = light_function<void()>>
using default_reactor = epoll_reactor<cb_t>;
#else
template <typename cb_t = light_function<void()>>
using default_reactor = poll_reactor<cb_t>;
#endif


} // namespace bfc

#endif // __BFC_DEFAULT_REACTOR_HPP__
