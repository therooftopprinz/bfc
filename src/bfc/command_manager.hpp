#ifndef __BFC_COMMAND_MANAGER_HPP__
#define __BFC_COMMAND_MANAGER_HPP__

#include <bfc/containers.hpp>

#include <regex>
#include <string>
#include <unordered_map>

#include <bfc/function.hpp>
#include <bfc/configuration_parser.hpp>

namespace bfc
{

template <typename cmd_cb_t = config::function<bfc::string(args_map&&)>>
class command_manager
{
public:
    template <typename cb_t>
    void add(const bfc::string& p_cmd, cb_t&& p_callback)
    {
        m_cmds.emplace(p_cmd, std::forward<cb_t>(p_callback));
    }

    void remove(const bfc::string& p_cmd)
    {
        m_cmds.erase(p_cmd);
    }

    bfc::string execute(const bfc::string& p_cmd)
    {
        using match_t = bfc::string_match;

        static const std::regex cmd_pattern("^([\\S]+)[\\s]*(.*)$");
        bfc::string cmd;
        bfc::string args;
        args_map map;

        match_t cmd_match;
        if (std::regex_match(p_cmd, cmd_match, cmd_pattern))
        {
            cmd.assign(cmd_match[1].first, cmd_match[1].second);
            args.assign(cmd_match[2].first, cmd_match[2].second);
        }

        static const std::regex args_pattern("([\\S]+)[\\s]*=[\\s]*([\\S]+)");
        match_t args_match;
        while (std::regex_search(args, args_match, args_pattern))
        {
            if (3 != args_match.size())
            {
                bfc::string out("invalid argument:");
                out.append(args_match[0].first, args_match[0].second);
                return out;
            }
            map.emplace(bfc::string(args_match[1].first, args_match[1].second),
                        bfc::string(args_match[2].first, args_match[2].second));
            bfc::string rest(args_match.suffix().first, args_match.suffix().second); // not an alias of args
            args = std::move(rest);
        }

        auto it = m_cmds.find(cmd);
        if (m_cmds.end() == it)
        {
            bfc::string out("command not found: \"");
            out += cmd;
            out += "\"";
            return out;
        }

        return it->second(std::move(map));
    }

private:
    bfc::unordered_map<bfc::string, cmd_cb_t, bfc::string_hash> m_cmds;
};

} // namespace bfc

#endif // __BFC_COMMAND_MANAGER_HPP__
