#ifndef __BFC_EPOLL_REACTOR_HPP__
#define __BFC_EPOLL_REACTOR_HPP__

#include <unordered_map>
#include <stdexcept>
#include <cstring>
#include <atomic>
#include <cerrno>
#include <string>
#include <thread>
#include <vector>
#include <limits>
#include <deque>
#include <mutex>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <bfc/function.hpp>
#include <bfc/timer.hpp>

namespace bfc
{

namespace detail
{

template <typename cb_t = light_function<void()>>
struct epoll_reactor
{
    struct fd_ctx_s
    {
        int fd = -1;
        epoll_event event{};
        cb_t cb = nullptr;
    };

    struct fd_entry_s
    {
        int fd = -1;           // original fd
        fd_ctx_s read_ctx;     // uses original fd
        fd_ctx_s write_ctx;    // uses dup(fd) when needed
        bool read_active = false;
        bool write_active = false;
    };

    epoll_reactor(const epoll_reactor&) = delete;
    void operator=(const epoll_reactor&) = delete;

    epoll_reactor(size_t p_cache_size = 64)
        : m_event_cache(p_cache_size)
        , m_epoll_fd(epoll_create1(0))
        , m_event_fd(-1)
        , m_running(false)
    {
        m_event_fd = eventfd(0, EFD_SEMAPHORE);

        if (-1 == m_event_fd)
        {
            close(m_epoll_fd);
            throw std::runtime_error(strerror(errno));
        }

        if (-1 == m_epoll_fd)
        {
            throw std::runtime_error(strerror(errno));
        }

        m_event_fd_ctx.fd = m_event_fd;
        m_event_fd_ctx.event.events = EPOLLIN;
        m_event_fd_ctx.event.data.ptr = &m_event_fd_ctx;
        m_event_fd_ctx.cb = [this](){
                uint64_t one;
                auto res [[maybe_unused]] = read(m_event_fd, &one, sizeof(one));
            };

        if (-1 == epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_event_fd_ctx.fd, &(m_event_fd_ctx.event)))
        {
            close(m_event_fd);
            close(m_epoll_fd);
            throw std::runtime_error(strerror(errno));
        }
    }

    ~epoll_reactor()
    {
        stop();
        close(m_event_fd);
        close(m_epoll_fd);
    }

    bool add_read(int fd, uint32_t events, cb_t cb)
    {
        auto& entry = m_fd_entries[fd];
        entry.fd = (entry.fd == -1) ? fd : entry.fd;

        auto& ctx = entry.read_ctx;
        ctx.fd = fd;
        ctx.event.events = events;
        ctx.event.data.ptr = &ctx;
        ctx.cb = std::move(cb);

        if (-1 == epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &(ctx.event)))
        {
            return false;
        }

        entry.read_active = true;
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
            auto res [[maybe_unused]] = epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, ctx.fd, nullptr);
            entry.read_active = false;
            ctx.cb = nullptr;
            ctx.fd = -1;
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

        ctx.event.events = 0;
        ctx.event.data.ptr = &ctx;
        ctx.cb = std::move(cb);

        if (-1 == epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, ctx.fd, &(ctx.event)))
        {
            return false;
        }

        entry.write_active = true;
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
            auto res [[maybe_unused]] = epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, ctx.fd, nullptr);
            close(ctx.fd);
            ctx.fd = -1;
            ctx.cb = nullptr;
            entry.write_active = false;
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

        ctx.event.events = events;
        return epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, ctx.fd, &(ctx.event)) == 0;
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
                    // convert microseconds to milliseconds for epoll_wait
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

            auto nfds = epoll_wait(m_epoll_fd, m_event_cache.data(), m_event_cache.size(), timeout_ms);
            if (-1 == nfds)
            {
                if (EINTR != errno)
                {
                    throw std::runtime_error(strerror(errno));
                }
                continue;
            }

            for (int i=0; i<nfds; i++)
            {
                auto& ev = m_event_cache[i];

                // Internal eventfd used for wakeups
                if (ev.data.ptr == &m_event_fd_ctx)
                {
                    if (m_event_fd_ctx.cb)
                    {
                        m_event_fd_ctx.cb();
                    }
                    continue;
                }

                auto* ctx = static_cast<fd_ctx_s*>(ev.data.ptr);
                if (ctx && ctx->cb)
                {
                    ctx->cb();
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

            // Clean up any entries that no longer have active interests
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

        uint64_t one = 1;
        auto res [[maybe_unused]] = write(m_event_fd, &one, sizeof(one));
    }

    timer<cb_t>& get_timer()
    {
        return m_timer;
    }

private:
    std::vector<epoll_event> m_event_cache;

    std::mutex m_wake_up_cb_mtx;
    std::vector<cb_t> m_wake_up_cb;

    int m_epoll_fd;
    int m_event_fd;
    std::atomic<bool> m_running;

    fd_ctx_s m_event_fd_ctx;

    std::unordered_map<int, fd_entry_s> m_fd_entries;
    std::vector<int> m_pending_cleanup;

    using timer_t = timer<cb_t>;
    timer_t m_timer;
};

} // namespace detail

template <typename cb_t = light_function<void()>>
class epoll_reactor
{
    using reactor_t = detail::epoll_reactor<cb_t>;

public:
    using fd_t = int;
    using timer_t = timer<cb_t>;

    epoll_reactor(const epoll_reactor&) = delete;
    void operator=(const epoll_reactor&) = delete;

    epoll_reactor() = default;
    ~epoll_reactor() = default;

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
        return m_reactor.add_read(fd, EPOLLIN|EPOLLRDHUP, std::move(cb));
    }

    bool rem_read_rdy(fd_t fd)
    {
        m_reactor.wake_up([this, fd](){
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
        m_reactor.wake_up([this, fd](){
                m_reactor.rem_write(fd);
            });
        return true;
    }

    bool req_write(fd_t fd)
    {
        return m_reactor.req_write(fd, EPOLLOUT|EPOLLONESHOT);
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

#endif // __BFC_EPOLL_REACTOR_HPP__
