#ifndef __BFC_DEFAULT_REACTOR_HPP__
#define __BFC_DEFAULT_REACTOR_HPP__

#include <bfc/config.hpp>

// default_reactor is the reactor the project's <bfc_config.hpp> selects:
// BFC_CONFIG_REACTOR_EPOLL -> epoll_reactor, BFC_CONFIG_REACTOR_POLL ->
// poll_reactor. There is no auto-detection; the choice is the project's.
#if defined(BFC_CONFIG_REACTOR_EPOLL)
#if defined(__has_include)
#if !__has_include(<sys/epoll.h>)
#error "bfc_config.hpp selects BFC_CONFIG_REACTOR_EPOLL, but this platform has no <sys/epoll.h>; use BFC_CONFIG_REACTOR_POLL"
#endif
#endif
#include <bfc/epoll_reactor.hpp>
#else
#include <bfc/poll_reactor.hpp>
#endif

#include <bfc/object_registry.hpp>

namespace bfc
{

#if defined(BFC_CONFIG_REACTOR_EPOLL)
template <typename cb_t = config::function<void()>>
using default_reactor = epoll_reactor<cb_t>;
#else
template <typename cb_t = config::function<void()>>
using default_reactor = poll_reactor<cb_t>;
#endif

using default_reactor_tag = object_tag<default_reactor<>>;
using default_reactor_registry = object_registry<default_reactor_tag>;

} // namespace bfc

#endif // __BFC_DEFAULT_REACTOR_HPP__
