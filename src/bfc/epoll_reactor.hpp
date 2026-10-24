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
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <bfc/function.hpp>
#include <bfc/timer.hpp>

// epoll_pwait2 (Linux 5.11+) gives microsecond timer precision; without it
// epoll_wait is used with the timeout rounded up to whole milliseconds.
// Define BFC_NO_EPOLL_PWAIT2 to force epoll_wait.
#if defined(BFC_NO_EPOLL_PWAIT2)
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 35))
#define BFC_EPOLL_PWAIT2 1
#elif defined(SYS_epoll_pwait2)
#define BFC_EPOLL_PWAIT2 1
#define BFC_EPOLL_PWAIT2_SYSCALL 1
#endif

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

    bool is_reactor_thread() const
    {
        // Written by run() on the reactor thread, read from any thread.
        const std::thread::id id = m_reactor_thread_id.load();
        return id != std::thread::id{} && id == std::this_thread::get_id();
    }

    void run()
    {
        m_reactor_thread_id = std::this_thread::get_id();
        m_running = true;
        while (m_running)
        {
            timespec timeout{};
            const timespec* timeout_ptr = nullptr;
            int64_t next_deadline_us = 0;
            bool has_deadline = m_timer.get_next_deadline_us(next_deadline_us);
            if (has_deadline)
            {
                auto now_us = timer_t::current_time_us();
                auto diff = next_deadline_us - now_us;
                if (diff > 0)
                {
                    timeout.tv_sec = static_cast<time_t>(diff / 1000000);
                    timeout.tv_nsec = static_cast<long>((diff % 1000000) * 1000);
                }
                timeout_ptr = &timeout;
            }

            auto nfds = wait(timeout_ptr);
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
                // Skip entries re-armed by add_read/add_write after removal
                if (it != m_fd_entries.end() && !it->second.read_active && !it->second.write_active)
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
        m_reactor_thread_id = std::thread::id{};
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
    int wait(const timespec* p_timeout)
    {
#if defined(BFC_EPOLL_PWAIT2_SYSCALL)
        return static_cast<int>(syscall(SYS_epoll_pwait2, m_epoll_fd, m_event_cache.data(), m_event_cache.size(), p_timeout, nullptr, size_t(0)));
#elif defined(BFC_EPOLL_PWAIT2)
        return epoll_pwait2(m_epoll_fd, m_event_cache.data(), m_event_cache.size(), p_timeout, nullptr);
#else
        int timeout_ms = -1;
        if (p_timeout)
        {
            int64_t ms = int64_t(p_timeout->tv_sec) * 1000 + (p_timeout->tv_nsec + 999999) / 1000000;
            timeout_ms = ms > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : int(ms);
        }
        return epoll_wait(m_epoll_fd, m_event_cache.data(), m_event_cache.size(), timeout_ms);
#endif
    }

    std::vector<epoll_event> m_event_cache;

    std::mutex m_wake_up_cb_mtx;
    std::vector<cb_t> m_wake_up_cb;

    int m_epoll_fd;
    int m_event_fd;
    std::atomic<bool> m_running;
    std::atomic<std::thread::id> m_reactor_thread_id{};

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

    bool rem_read_rdy(fd_t fd, cb_t done_cb = nullptr)
    {
        // On the reactor thread remove now: the caller may close the fd and free the
        // callback's owner while an event for this fd is still pending in the current batch.
        if (m_reactor.is_reactor_thread())
        {
            m_reactor.rem_read(fd);
            if (done_cb)
            {
                done_cb();
            }
            return true;
        }
        m_reactor.wake_up([this, fd, done_cb = std::move(done_cb)]() mutable {
                m_reactor.rem_read(fd);
                if (done_cb)
                {
                    done_cb();
                }
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

    bool rem_write_rdy(fd_t fd, cb_t done_cb = nullptr)
    {
        // On the reactor thread remove now: the caller may close the fd and free the
        // callback's owner while an event for this fd is still pending in the current batch.
        if (m_reactor.is_reactor_thread())
        {
            m_reactor.rem_write(fd);
            if (done_cb)
            {
                done_cb();
            }
            return true;
        }
        m_reactor.wake_up([this, fd, done_cb = std::move(done_cb)]() mutable {
                m_reactor.rem_write(fd);
                if (done_cb)
                {
                    done_cb();
                }
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

    // @brief Smallest timer step the reactor can wait for, in microseconds.
    // Timers due sooner than this after the previous wait may fire late.
    static constexpr uint64_t minimum_timer_step()
    {
#if defined(BFC_EPOLL_PWAIT2)
        return 1;
#else
        return 1000;
#endif
    }

    bool is_reactor_thread() const
    {
        return m_reactor.is_reactor_thread();
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
