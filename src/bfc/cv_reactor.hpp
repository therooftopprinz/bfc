#ifndef __BFC_CV_REACTOR_HPP__
#define __BFC_CV_REACTOR_HPP__

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <list>
#include <vector>
#include <algorithm>

#include <bfc/function.hpp>
#include <bfc/event_queue.hpp>
#include <bfc/timer.hpp>

namespace bfc
{

template <typename cb_t = light_function<void()>>
class cv_reactor
{
public:
    using context = reactive_event_queue_base<cb_t>;
    using timer_t = timer<cb_t>;
    using callback_t = cb_t;

    cv_reactor(const cv_reactor&) = delete;
    void operator=(const cv_reactor&) = delete;

    cv_reactor(uint64_t timeout=100)
        : m_timeout_ms(timeout)
    {}

    ~cv_reactor()
    {
        stop();
    }

    timer_t& get_timer()
    {
        return m_timer;
    }

    bool add_read_rdy(context& ctx, cb_t cb)
    {
        ctx.set_callback(std::move(cb));

        std::unique_lock<std::mutex> lg(m_ctx_mtx);
        auto it = std::find(m_contexts.begin(), m_contexts.end(), &ctx);
        if (it == m_contexts.end())
        {
            m_contexts.push_back(&ctx);
        }
        return true;
    }

    bool remove_read_rdy(context& ctx)
    {
        ctx.set_callback(nullptr);

        std::unique_lock<std::mutex> lg(m_ctx_mtx);
        m_contexts.remove(&ctx);
        return true;
    }

    // @brief Smallest timer step the reactor can wait for, in microseconds.
    // Timers due sooner than this after the previous wait may fire late.
    // condition_variable waits are only as fine as the platform clock (e.g.
    // FreeRTOS ticks), so only Linux claims microseconds.
    static constexpr uint64_t minimum_timer_step()
    {
#if defined(__linux__)
        return 1;
#else
        return 1000;
#endif
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
            int64_t next_deadline_us = 0;
            bool has_deadline = m_timer.get_next_deadline_us(next_deadline_us);
            auto now_us = timer_t::current_time_us();
            int64_t timeout_us = static_cast<int64_t>(m_timeout_ms) * 1000;
            if (has_deadline)
            {
                auto diff = next_deadline_us - now_us;
                if (diff <= 0)
                {
                    timeout_us = 0;
                }
                else if (diff < timeout_us)
                {
                    timeout_us = diff;
                }
            }

            {
                std::unique_lock<std::mutex> lg(m_wakeup_mtx);

                m_cv.wait_for(lg, std::chrono::microseconds(timeout_us), [this]()
                    {
                        return m_wakeup_req || !m_running;
                    });

                if (!m_running)
                {
                    break;
                }

                if (m_wakeup_req)
                {
                    m_wakeup_req = false;
                    lg.unlock();

                    {
                        std::unique_lock<std::mutex> ctx_lg(m_ctx_mtx);
                        m_ctx_snapshot.assign(m_contexts.begin(), m_contexts.end());
                    }

                    for (auto* ctx : m_ctx_snapshot)
                    {
                        if (!ctx || !ctx->has_data())
                        {
                            continue;
                        }
                        ctx->notify_callback();
                    }
                }
            }

            {
                std::unique_lock<std::mutex> lg(m_wake_up_cb_mtx);
                auto cbl = std::move(m_wake_up_cb);
                lg.unlock();

                for (auto& cb : cbl)
                {
                    cb();
                }
            }

            m_timer.schedule(timer_t::current_time_us());
        }
        m_reactor_thread_id = std::thread::id{};
    }

    void wake_up(cb_t cb = nullptr)
    {
        if (cb)
        {
            std::unique_lock<std::mutex> lg(m_wake_up_cb_mtx);
            m_wake_up_cb.emplace_back(std::move(cb));
        }

        std::unique_lock<std::mutex> lg(m_wakeup_mtx);
        m_wakeup_req = true;
        m_cv.notify_one();
    }

    void stop()
    {
        m_running = false;
        wake_up();
    }

 private:
    size_t m_timeout_ms = 100;

    timer_t m_timer;

    std::mutex m_wakeup_mtx;
    bool m_wakeup_req = false;
    std::condition_variable m_cv;

    std::mutex m_ctx_mtx;
    std::list<context*> m_contexts;
    // run()'s per-wake copy of m_contexts; reused so a wake does not allocate.
    std::vector<context*> m_ctx_snapshot;

    std::mutex m_wake_up_cb_mtx;
    std::vector<cb_t> m_wake_up_cb;

    std::atomic<bool> m_running{false};
    std::atomic<std::thread::id> m_reactor_thread_id{};
};

using cv_reactor_tag = object_tag<cv_reactor<>>;
using cv_reactor_registry = object_registry<cv_reactor_tag>;

} // namespace bfc

#endif // __BFC_CV_REACTOR_HPP__
