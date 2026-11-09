#pragma once
// bfc's own tests with the default allocator: every bfc container is the same
// type as its std:: counterpart.
#include <functional>
#include <memory>

#define BFC_CONFIG_ID "bfc-tests-std"
#define BFC_CONFIG_REACTOR_EPOLL 1

namespace bfc::config
{
template <typename Signature>
using function = std::function<Signature>;
template <typename T>
using allocator = std::allocator<T>;
} // namespace bfc::config
