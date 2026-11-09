#ifndef __BFC_CONFIG_HPP__
#define __BFC_CONFIG_HPP__

// The project's bfc configuration.
//
// The top-level project (the firmware or application, never a library that
// bfc is built into) provides <bfc_config.hpp> and puts its directory on the
// include path through the `bfc_config` CMake target. bfc has no fallback: a
// missing config is a compile error, so no part of a build can quietly use
// other types than the rest.
//
// <bfc_config.hpp> defines:
//
//   BFC_CONFIG_ID                    a string naming the config (for diagnostics)
//   exactly one of
//     BFC_CONFIG_REACTOR_POLL        default_reactor is poll_reactor
//     BFC_CONFIG_REACTOR_EPOLL       default_reactor is epoll_reactor
//   in namespace bfc::config, the allocator type bfc uses for payload bytes,
//   control nodes and queue slots (stateless, std-compatible):
//     template <typename T> using allocator = ...;
//   It must honour alignof(T), including over-aligned T (memory_pool asks for
//   16- or 64-byte blocks).
//   Callbacks stay on the global heap: std::function captures (including a
//   capturing release callback given to shared_sized_buffer::adopt) are not
//   placed by the allocator.
//   and the callback types every default uses:
//     reactor_cb                     reactor callbacks (cv, poll, epoll, default)
//     queue_cb                       reactive queue callbacks
//     timer_cb                       timer callbacks
//
// Example (a project that wants std::function callbacks on a poll reactor):
//
//   #pragma once
//   #include <functional>
//   #define BFC_CONFIG_ID "my-firmware"
//   #define BFC_CONFIG_REACTOR_POLL 1
//   namespace bfc::config
//   {
//   using reactor_cb = std::function<void()>;
//   using queue_cb = std::function<void()>;
//   using timer_cb = std::function<void()>;
//   template <typename T> using allocator = std::allocator<T>;
//   } // namespace bfc::config

#if defined(__has_include)
#if !__has_include(<bfc_config.hpp>)
#error "bfc needs a project <bfc_config.hpp> (see bfc/config.hpp); link the bfc_config target"
#endif
#endif

#include <bfc_config.hpp>

#include <cstddef>
#include <type_traits>

#ifndef BFC_CONFIG_ID
#error "bfc_config.hpp must define BFC_CONFIG_ID"
#endif

#if defined(BFC_CONFIG_REACTOR_POLL) && defined(BFC_CONFIG_REACTOR_EPOLL)
#error "bfc_config.hpp defines both BFC_CONFIG_REACTOR_POLL and BFC_CONFIG_REACTOR_EPOLL"
#endif
#if !defined(BFC_CONFIG_REACTOR_POLL) && !defined(BFC_CONFIG_REACTOR_EPOLL)
#error "bfc_config.hpp must define BFC_CONFIG_REACTOR_POLL or BFC_CONFIG_REACTOR_EPOLL"
#endif

namespace bfc
{
namespace config
{

static_assert(std::is_invocable_r_v<void, reactor_cb &>, "bfc::config::reactor_cb must be callable as void()");
static_assert(std::is_invocable_r_v<void, queue_cb &>, "bfc::config::queue_cb must be callable as void()");
static_assert(std::is_invocable_r_v<void, timer_cb &>, "bfc::config::timer_cb must be callable as void()");
static_assert(std::is_same_v<typename allocator<int>::value_type, int>, "bfc::config::allocator<T> must be an allocator of T");
static_assert(std::is_empty_v<allocator<int>>, "bfc::config::allocator<T> must be stateless");
static_assert(std::is_nothrow_default_constructible_v<allocator<int>>, "bfc::config::allocator<T> must be default constructible");

} // namespace config
} // namespace bfc

#endif // __BFC_CONFIG_HPP__
