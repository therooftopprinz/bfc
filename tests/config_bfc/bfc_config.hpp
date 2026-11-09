#pragma once
// bfc's own tests with bfc's fixed-size function types and no std::function:
// callbacks live inline, nothing in bfc depends on std::function.
#include <cstddef>
#include <memory>

#include <bfc/function.hpp>

#define BFC_CONFIG_ID "bfc-tests-bfc"
#define BFC_CONFIG_REACTOR_EPOLL 1

namespace bfc::config
{
template <typename Signature>
using function = bfc::large_function<Signature>;
template <typename T>
using allocator = std::allocator<T>;
} // namespace bfc::config
