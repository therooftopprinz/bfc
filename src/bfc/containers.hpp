#ifndef __BFC_CONTAINERS_HPP__
#define __BFC_CONTAINERS_HPP__

// std containers over the project's bfc::config::allocator<T>. With the
// allocator aliased to std::allocator (the default for a project that does not
// care) every alias here is the same type as its std:: counterpart, so nothing
// changes for callers. A project with its own allocator gets containers that
// use it, and names them through these aliases.

#include <bfc/config.hpp>

#include <functional>
#include <list>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bfc
{

template <typename T>
using vector = std::vector<T, config::allocator<T>>;

template <typename T>
using list = std::list<T, config::allocator<T>>;

template <typename K, typename V, typename compare_t = std::less<K>>
using map = std::map<K, V, compare_t, config::allocator<std::pair<const K, V>>>;

template <typename K, typename V, typename hash_t = std::hash<K>, typename equal_t = std::equal_to<K>>
using unordered_map = std::unordered_map<K, V, hash_t, equal_t, config::allocator<std::pair<const K, V>>>;

using string = std::basic_string<char, std::char_traits<char>, config::allocator<char>>;

// Hash for bfc::string keys. C++17 only guarantees std::hash for
// std::basic_string with std::allocator; this hashes the characters.
// Regex match results over bfc::string, on the config allocator (std::regex
// keeps its own compiled state on the global heap).
using string_match = std::match_results<string::const_iterator,
                                        config::allocator<std::sub_match<string::const_iterator>>>;

struct string_hash
{
    std::size_t operator()(const string &s) const noexcept
    {
        return std::hash<std::string_view>{}(std::string_view(s.data(), s.size()));
    }
};

using istringstream = std::basic_istringstream<char, std::char_traits<char>, config::allocator<char>>;
using ostringstream = std::basic_ostringstream<char, std::char_traits<char>, config::allocator<char>>;

} // namespace bfc

#endif // __BFC_CONTAINERS_HPP__
