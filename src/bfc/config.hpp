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
//   Callback captures are stored by config::function, not the allocator: inline
//   for a fixed-size function, on the global heap for std::function captures
//   that do not fit its small buffer. std::regex state is also global heap.
//   and the one function template every callback in bfc uses (reactor, timer,
//   queue, thread pool, command and release callbacks, bfc's own internals):
//     template <typename Signature> using function = ...;
//   either std::function or a fixed-size function large enough for the
//   captures (bfc::large_function holds 64 bytes inline; bfc's own internal
//   callbacks capture at most a few words). It must be constructible from a
//   lambda, testable for empty, and callable as the signature.
//
// Example (a project that wants std::function callbacks on a poll reactor):
//
//   #pragma once
//   #include <functional>
//   #define BFC_CONFIG_ID "my-firmware"
//   #define BFC_CONFIG_REACTOR_POLL 1
//   namespace bfc::config
//   {
//   template <typename Signature>
//   using function = std::function<Signature>;
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

static_assert(std::is_invocable_r_v<void, function<void()> &>, "bfc::config::function<void()> must be callable");
static_assert(std::is_invocable_r_v<void, function<void(std::byte *)> &, std::byte *>, "bfc::config::function<void(std::byte *)> must be callable");
static_assert(std::is_constructible_v<bool, const function<void()> &>, "bfc::config::function must be testable for empty (explicit operator bool)");
static_assert(std::is_same_v<typename allocator<int>::value_type, int>, "bfc::config::allocator<T> must be an allocator of T");
static_assert(std::is_empty_v<allocator<int>>, "bfc::config::allocator<T> must be stateless");
static_assert(std::is_nothrow_default_constructible_v<allocator<int>>, "bfc::config::allocator<T> must be default constructible");

} // namespace config
} // namespace bfc

#endif // __BFC_CONFIG_HPP__
