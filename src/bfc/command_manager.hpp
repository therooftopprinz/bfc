#ifndef __BFC_COMMAND_MANAGER_HPP__
#define __BFC_COMMAND_MANAGER_HPP__

#include <regex>
#include <string>
#include <unordered_map>

#include <bfc/function.hpp>
#include <bfc/configuration_parser.hpp>

namespace bfc
{

template <typename cmd_cb_t = light_function<std::string(args_map&&)>>
class command_manager
{
public:
    template <typename cb_t>
    void add(const std::string& p_cmd, cb_t&& p_callback)
    {
        m_cmds.emplace(p_cmd, std::forward<cb_t>(p_callback));
    }

    void remove(const std::string& p_cmd)
    {
        m_cmds.erase(p_cmd);
    }

    std::string execute(const std::string& p_cmd)
    {
        using namespace std::string_literals;

        static const std::regex cmd_pattern("^([\\S]+)[\\s]*(.*)$");
        std::string cmd;
        std::string args;
        args_map map;

        std::smatch cmd_match;
        if (std::regex_match(p_cmd, cmd_match, cmd_pattern))
        {
            cmd = cmd_match[1].str();
            args = cmd_match[2].str();
        }

        static const std::regex args_pattern("([\\S]+)[\\s]*=[\\s]*([\\S]+)");
        std::smatch args_match;
        while (std::regex_search(args, args_match, args_pattern))
        {
            if (3 != args_match.size())
            {
                return "invalid argument:"s + args_match[0].str();
            }
            map.emplace(args_match[1], args_match[2]);
            args = args_match.suffix();
        }

        auto it = m_cmds.find(cmd);
        if (m_cmds.end() == it)
        {
            return "command not found: \""s + cmd + "\"";
        }

        return it->second(std::move(map));
    }

private:
    std::unordered_map<std::string, cmd_cb_t> m_cmds;
};

} // namespace bfc

#endif // __BFC_COMMAND_MANAGER_HPP__
