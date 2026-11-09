#ifndef __BFC_CONFIGURATION_PARSER_HPP__
#define __BFC_CONFIGURATION_PARSER_HPP__

#include <bfc/containers.hpp>

#include <optional>
#include <utility>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <regex>
#include <map>

namespace bfc
{

namespace detail
{
inline bfc::string trim(const bfc::string& s)
{
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == bfc::string::npos)
        return bfc::string();
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
} // namespace detail

class args_map : public bfc::map<bfc::string, bfc::string, std::less<>>
{
public:
    args_map() = default;

    template<typename T>
    std::optional<T> as(const std::string_view& p_key) const
    {
        auto findit = find(p_key);
        if (findit == end())
        {
            return std::nullopt;
        }

        T rv;
        bfc::istringstream iss(findit->second);
        iss >> rv;
        if (iss.fail())
        {
            return std::nullopt;
        }

        return rv;
    }

    std::optional<bfc::string> arg(const std::string_view& p_key) const
    {
        auto findit = find(p_key);
        if (findit == end())
        {
            return std::nullopt;
        }
        return findit->second;
    }
};

class configuration_parser : public args_map
{
public:
    void load_line(const bfc::string& line)
    {
        static const std::regex config_fmt("^(.+?)[ ]*=[ ]*(.*?)$");
        bfc::string_match match;
        if (std::regex_match(line, match, config_fmt))
        {
            try_emplace(detail::trim(bfc::string(match[1].first, match[1].second)),
                        detail::trim(bfc::string(match[2].first, match[2].second)));
        }
    }

    bool load(const bfc::string& file)
    {
        std::ifstream in(file.c_str());
        if (!in.is_open())
        {
            return false;
        }

        bfc::string line;
        while (std::getline(in, line))
        {
            load_line(line);
        }
        return true;
    }
};

} // namespace bfc

#endif // __BFC_CONFIGURATION_PARSER_HPP__
