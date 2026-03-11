#ifndef __BFC_CONFIGURATION_PARSER_HPP__
#define __BFC_CONFIGURATION_PARSER_HPP__

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
inline std::string trim(const std::string& s)
{
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
} // namespace detail

class args_map : public std::map<std::string, std::string, std::less<>>
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
        std::istringstream iss(findit->second);
        iss >> rv;
        if (iss.fail())
        {
            return std::nullopt;
        }

        return rv;
    }

    std::optional<std::string> arg(const std::string_view& p_key) const
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
    void load_line(const std::string& line)
    {
        static const std::regex config_fmt("^(.+?)[ ]*=[ ]*(.*?)$");
        std::smatch match;
        if (std::regex_match(line, match, config_fmt))
        {
            try_emplace(detail::trim(match[1].str()), detail::trim(match[2].str()));
        }
    }

    bool load(const std::string& file)
    {
        std::ifstream in(file);
        if (!in.is_open())
        {
            return false;
        }

        std::string line;
        while (std::getline(in, line))
        {
            load_line(line);
        }
        return true;
    }
};

} // namespace bfc

#endif // __BFC_CONFIGURATION_PARSER_HPP__
