#ifndef __BFC_POLL_REACTOR_HPP__
#define __BFC_POLL_REACTOR_HPP__

#include <unordered_map>
#include <stdexcept>
#include <cstring>
#include <atomic>
#include <cerrno>
#include <string>
#include <vector>
#include <limits>
#include <mutex>

#include <poll.h>
#include <unistd.h>
#include <fcntl.h>

#include <bfc/function.hpp>
#include <bfc/timer.hpp>

namespace bfc
{

namespace detail
{

template <typename cb_t = light_function<void()>>
struct poll_reactor
{
    enum class poll_kind : uint8_t
    {
        wake = 0,
        read,
        write,
    };

    struct fd_ctx_s
    {
        int fd = -1;
        cb_t cb = nullptr;
    };

    struct fd_entry_s
    {
        int fd = -1;           // original fd
        fd_ctx_s read_ctx;     // uses original fd
        fd_ctx_s write_ctx;    // uses dup(fd) when needed
        bool read_active = false;
        bool write_active = false;
        size_t read_poll_index = invalid_index();
        size_t write_poll_index = invalid_index();

        static constexpr size_t invalid_index()
        {
            return std::numeric_limits<size_t>::max();
        }
    };

    struct poll_entry_s
    {
        pollfd pfd{};
        int owner_fd = -1;
        poll_kind kind = poll_kind::wake;
    };

    poll_reactor(const poll_reactor&) = delete;
    void operator=(const poll_reactor&) = delete;

    poll_reactor(size_t = 64)
        : m_running(false)
    {
        if (pipe(m_wake_pipe) == -1)
        {
            throw std::runtime_error(strerror(errno));
        }

        set_non_blocking(m_wake_pipe[0]);
        set_non_blocking(m_wake_pipe[1]);

        poll_entry_s wake_entry;
        wake_entry.pfd.fd = m_wake_pipe[0];
        wake_entry.pfd.events = POLLIN;
        wake_entry.pfd.revents = 0;
        wake_entry.owner_fd = -1;
        wake_entry.kind = poll_kind::wake;
        m_poll_entries.emplace_back(wake_entry);

        m_wake_cb = [this]() {
            char tmp[64];
            while (true)
            {
                auto n = read(m_wake_pipe[0], tmp, sizeof(tmp));
                if (n > 0)
                {
                    continue;
                }
                if (n == -1 && errno == EINTR)
                {
                    continue;
                }
                break;
            }
            // A wake signal has been consumed (or was already pending via a full pipe).
            m_wake_pending.store(false, std::memory_order_release);
        };
    }

    ~poll_reactor()
    {
        stop();
        close(m_wake_pipe[0]);
        close(m_wake_pipe[1]);
    }

    bool add_read(int fd, uint32_t events, cb_t cb)
    {
        auto& entry = m_fd_entries[fd];
        entry.fd = (entry.fd == -1) ? fd : entry.fd;

        auto& ctx = entry.read_ctx;
        ctx.fd = fd;
        ctx.cb = std::move(cb);

        entry.read_active = true;
        if (entry.read_poll_index == fd_entry_s::invalid_index())
        {
            entry.read_poll_index = add_poll_entry(fd, fd, poll_kind::read, static_cast<short>(events));
        }
        else
        {
            set_poll_entry(entry.read_poll_index, fd, static_cast<short>(events));
        }
        return true;
    }

    bool rem_read(int fd)
    {
        auto it = m_fd_entries.find(fd);
        if (it == m_fd_entries.end())
        {
            return false;
        }

        auto& entry = it->second;
        auto& ctx = entry.read_ctx;
        if (entry.read_active && ctx.fd != -1)
        {
            remove_poll_entry_by_index(entry.read_poll_index);
            entry.read_active = false;
            ctx.cb = nullptr;
            ctx.fd = -1;
            entry.read_poll_index = fd_entry_s::invalid_index();
        }

        if (!entry.read_active && !entry.write_active)
        {
            m_pending_cleanup.push_back(fd);
        }

        return true;
    }

    bool add_write(int fd, cb_t cb)
    {
        auto& entry = m_fd_entries[fd];
        entry.fd = (entry.fd == -1) ? fd : entry.fd;

        auto& ctx = entry.write_ctx;
        if (ctx.fd == -1)
        {
            ctx.fd = dup(fd);
            if (ctx.fd == -1)
            {
                return false;
            }
        }

        ctx.cb = std::move(cb);
        entry.write_active = true;
        if (entry.write_poll_index == fd_entry_s::invalid_index())
        {
            entry.write_poll_index = add_poll_entry(ctx.fd, fd, poll_kind::write, 0);
        }
        else
        {
            set_poll_entry(entry.write_poll_index, ctx.fd, 0);
        }
        return true;
    }

    bool rem_write(int fd)
    {
        auto it = m_fd_entries.find(fd);
        if (it == m_fd_entries.end())
        {
            return false;
        }

        auto& entry = it->second;
        auto& ctx = entry.write_ctx;
        if (entry.write_active && ctx.fd != -1)
        {
            remove_poll_entry_by_index(entry.write_poll_index);
            close(ctx.fd);
            ctx.fd = -1;
            ctx.cb = nullptr;
            entry.write_active = false;
            entry.write_poll_index = fd_entry_s::invalid_index();
        }

        if (!entry.read_active && !entry.write_active)
        {
            m_pending_cleanup.push_back(fd);
        }

        return true;
    }

    bool req_write(int fd, uint32_t events)
    {
        auto it = m_fd_entries.find(fd);
        if (it == m_fd_entries.end())
        {
            return false;
        }

        auto& entry = it->second;
        auto& ctx = entry.write_ctx;
        if (!entry.write_active || ctx.fd == -1)
        {
            return false;
        }

        set_poll_events(entry.write_poll_index, static_cast<short>(events));
        return true;
    }

    void run()
    {
        m_running = true;
        while (m_running)
        {
            int timeout_ms = -1;
            int64_t next_deadline_us = 0;
            bool has_deadline = m_timer.get_next_deadline_us(next_deadline_us);
            if (has_deadline)
            {
                auto now_us = timer_t::current_time_us();
                auto diff = next_deadline_us - now_us;
                if (diff <= 0)
                {
                    timeout_ms = 0;
                }
                else
                {
                    auto diff_ms = diff / 1000;
                    if (diff_ms > static_cast<int64_t>(std::numeric_limits<int>::max()))
                    {
                        timeout_ms = std::numeric_limits<int>::max();
                    }
                    else
                    {
                        timeout_ms = static_cast<int>(diff_ms);
                    }
                }
            }

            // poll mutates revents, so build a temporary list each iteration.
            std::vector<pollfd> poll_fds;
            poll_fds.reserve(m_poll_entries.size());
            for (auto& entry : m_poll_entries)
            {
                entry.pfd.revents = 0;
                poll_fds.emplace_back(entry.pfd);
            }

            auto nfds = ::poll(poll_fds.data(), poll_fds.size(), timeout_ms);
            if (nfds == -1)
            {
                if (EINTR != errno)
                {
                    throw std::runtime_error(strerror(errno));
                }
                continue;
            }

            if (nfds > 0)
            {
                for (size_t i = 0; i < poll_fds.size(); i++)
                {
                    auto revents = poll_fds[i].revents;
                    if (revents == 0)
                    {
                        continue;
                    }

                    auto item = m_poll_entries[i];
                    if (item.kind == poll_kind::wake)
                    {
                        if (m_wake_cb)
                        {
                            m_wake_cb();
                        }
                        continue;
                    }

                    auto it = m_fd_entries.find(item.owner_fd);
                    if (it == m_fd_entries.end())
                    {
                        continue;
                    }

                    if (item.kind == poll_kind::read)
                    {
                        auto& ctx = it->second.read_ctx;
                        if (ctx.cb)
                        {
                            ctx.cb();
                        }
                    }
                    else
                    {
                        auto& ctx = it->second.write_ctx;
                        if (ctx.cb)
                        {
                            // emulate one-shot write notifications: callback must re-arm via req_write().
                            set_poll_events(it->second.write_poll_index, 0);
                            ctx.cb();
                        }
                    }
                }
            }

            {
                std::unique_lock lg(m_wake_up_cb_mtx);
                auto cbl = std::move(m_wake_up_cb);
                lg.unlock();

                for (auto& cb : cbl)
                {
                    cb();
                }
            }

            for (auto fd : m_pending_cleanup)
            {
                auto it = m_fd_entries.find(fd);
                if (it != m_fd_entries.end())
                {
                    auto& entry = it->second;
                    if (entry.write_ctx.fd != -1)
                    {
                        close(entry.write_ctx.fd);
                    }
                    m_fd_entries.erase(it);
                }
            }
            m_pending_cleanup.clear();

            m_timer.schedule(timer_t::current_time_us());
        } // while (m_running)
    }

    void stop()
    {
        m_running = false;
        wake_up();
    }

    void wake_up(cb_t cb = nullptr)
    {
        if (cb)
        {
            std::unique_lock lg(m_wake_up_cb_mtx);
            m_wake_up_cb.emplace_back(std::move(cb));
        }

        bool expected = false;
        if (!m_wake_pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return;
        }

        char one = 1;
        while (true)
        {
            auto res = write(m_wake_pipe[1], &one, sizeof(one));
            if (res == sizeof(one))
            {
                return;
            }
            if (res == -1 && errno == EINTR)
            {
                continue;
            }
            if (res == -1 && errno == EAGAIN)
            {
                // Pipe is full, which still implies a pending wake signal.
                return;
            }
            // Unexpected failure: let future wake attempts try again.
            m_wake_pending.store(false, std::memory_order_release);
            return;
        }
    }

    timer<cb_t>& get_timer()
    {
        return m_timer;
    }

private:
    void set_non_blocking(int fd)
    {
        auto flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            throw std::runtime_error(strerror(errno));
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            throw std::runtime_error(strerror(errno));
        }
    }

    size_t add_poll_entry(int watched_fd, int owner_fd, poll_kind kind, short events)
    {
        poll_entry_s entry;
        entry.pfd.fd = watched_fd;
        entry.pfd.events = events;
        entry.pfd.revents = 0;
        entry.owner_fd = owner_fd;
        entry.kind = kind;
        m_poll_entries.emplace_back(entry);
        return m_poll_entries.size() - 1;
    }

    void set_poll_entry(size_t index, int watched_fd, short events)
    {
        if (index >= m_poll_entries.size())
        {
            return;
        }
        m_poll_entries[index].pfd.fd = watched_fd;
        m_poll_entries[index].pfd.events = events;
        m_poll_entries[index].pfd.revents = 0;
    }

    void set_poll_events(size_t index, short events)
    {
        if (index >= m_poll_entries.size())
        {
            return;
        }
        m_poll_entries[index].pfd.events = events;
        m_poll_entries[index].pfd.revents = 0;
    }

    void remove_poll_entry_by_index(size_t index)
    {
        if (index >= m_poll_entries.size())
        {
            return;
        }

        const size_t last = m_poll_entries.size() - 1;
        if (index != last)
        {
            auto moved = m_poll_entries[last];
            m_poll_entries[index] = moved;
            update_poll_index(moved.owner_fd, moved.kind, index);
        }
        m_poll_entries.pop_back();
    }

    void update_poll_index(int owner_fd, poll_kind kind, size_t new_index)
    {
        auto it = m_fd_entries.find(owner_fd);
        if (it == m_fd_entries.end())
        {
            return;
        }

        if (kind == poll_kind::read)
        {
            it->second.read_poll_index = new_index;
        }
        else if (kind == poll_kind::write)
        {
            it->second.write_poll_index = new_index;
        }
    }

    std::vector<poll_entry_s> m_poll_entries;

    std::mutex m_wake_up_cb_mtx;
    std::vector<cb_t> m_wake_up_cb;

    int m_wake_pipe[2]{-1, -1};
    cb_t m_wake_cb = nullptr;
    std::atomic<bool> m_wake_pending{false};
    std::atomic<bool> m_running;

    std::unordered_map<int, fd_entry_s> m_fd_entries;
    std::vector<int> m_pending_cleanup;

    using timer_t = timer<cb_t>;
    timer_t m_timer;
};

} // namespace detail

template <typename cb_t = light_function<void()>>
class poll_reactor
{
    using reactor_t = detail::poll_reactor<cb_t>;

public:
    using fd_t = int;
    using timer_t = timer<cb_t>;

    poll_reactor(const poll_reactor&) = delete;
    void operator=(const poll_reactor&) = delete;

    poll_reactor() = default;
    ~poll_reactor() = default;

    int get_last_error_code()
    {
        return errno;
    }

    std::string get_last_error()
    {
        return strerror(errno);
    }

    bool add_read_rdy(fd_t fd, cb_t cb)
    {
#ifdef POLLRDHUP
        constexpr uint32_t read_events = POLLIN | POLLRDHUP;
#else
        constexpr uint32_t read_events = POLLIN;
#endif
        return m_reactor.add_read(fd, read_events, std::move(cb));
    }

    bool rem_read_rdy(fd_t fd)
    {
        m_reactor.wake_up([this, fd]() {
            m_reactor.rem_read(fd);
        });
        return true;
    }

    bool req_read(fd_t)
    {
        return true;
    }

    bool add_write_rdy(fd_t fd, cb_t cb)
    {
        return m_reactor.add_write(fd, std::move(cb));
    }

    bool rem_write_rdy(fd_t fd)
    {
        m_reactor.wake_up([this, fd]() {
            m_reactor.rem_write(fd);
        });
        return true;
    }

    bool req_write(fd_t fd)
    {
        return m_reactor.req_write(fd, POLLOUT);
    }

    void wake_up(cb_t cb)
    {
        m_reactor.wake_up(std::move(cb));
    }

    void run()
    {
        m_reactor.run();
    }

    void stop()
    {
        m_reactor.stop();
    }

    timer_t& get_timer()
    {
        return m_reactor.get_timer();
    }

private:
    reactor_t m_reactor;
};

} // namespace bfc

#endif // __BFC_POLL_REACTOR_HPP__
