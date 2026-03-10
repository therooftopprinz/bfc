#ifndef __BFC_CV_REACTOR_HPP__
#define __BFC_CV_REACTOR_HPP__

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <list>
#include <algorithm>

#include <bfc/function.hpp>
#include <bfc/event_queue.hpp>
#include <bfc/timer.hpp>

namespace bfc
{

template <typename T, typename cb_t = light_function<void()>>
class cv_reactor
{
public:
    using context = reactive_event_queue<T, cv_reactor<T, cb_t>>;
    using timer_t = timer<cb_t>;

    cv_reactor(const cv_reactor&) = delete;
    void operator=(const cv_reactor&) = delete;

    cv_reactor(uint64_t timeout=100)
        : m_timeout_ms(timeout)
    {}

    ~cv_reactor()
    {
        stop();
    }

    template <typename... Ts>
    context make_context(Ts&&... ts)
    {
        return context(std::forward<Ts>(ts)...);
    }

    timer_t& get_timer()
    {
        return m_timer;
    }

    bool add_read_rdy(context& ctx, cb_t cb)
    {
        {
            std::unique_lock lg(ctx.cb_mtx);
            ctx.cb = std::move(cb);
        }

        std::unique_lock lg(m_ctx_mtx);
        auto it = std::find(m_contexts.begin(), m_contexts.end(), &ctx);
        if (it == m_contexts.end())
        {
            m_contexts.push_back(&ctx);
        }
        return true;
    }

    bool remove_read_rdy(context& ctx)
    {
        {
            std::unique_lock lg(ctx.cb_mtx);
            ctx.cb = nullptr;
        }

        std::unique_lock lg(m_ctx_mtx);
        m_contexts.remove(&ctx);
        return true;
    }

    void run()
    {
        m_running = true;
        while (m_running)
        {
            int64_t next_deadline_ms = 0;
            bool has_deadline = m_timer.get_next_deadline_ms(next_deadline_ms);
            auto now_ms = timer_t::current_time_ms();
            auto timeout_ms = m_timeout_ms;
            if (has_deadline)
            {
                auto diff = next_deadline_ms - now_ms;
                if (diff <= 0)
                {
                    timeout_ms = 0;
                }
                else if (static_cast<size_t>(diff) < timeout_ms)
                {
                    timeout_ms = static_cast<size_t>(diff);
                }
            }

            {
                std::unique_lock lg(m_wakeup_mtx);

                m_cv.wait_for(lg, std::chrono::milliseconds(timeout_ms), [this]()
                    {
                        return m_wakeup_req;
                    });

                if (m_wakeup_req)
                {
                    m_wakeup_req = false;
                    lg.unlock();

                    std::list<context*> ctxs;
                    {
                        std::unique_lock ctx_lg(m_ctx_mtx);
                        ctxs = m_contexts;
                    }

                    for (auto* ctx : ctxs)
                    {
                        if (!ctx)
                        {
                            continue;
                        }

                        if (ctx->size() == 0)
                        {
                            continue;
                        }

                        cb_t cb;
                        {
                            std::unique_lock cb_lg(ctx->cb_mtx);
                            cb = ctx->cb;
                        }

                        if (cb)
                        {
                            cb();
                        }
                    }
                }
            }

            m_timer.schedule(timer_t::current_time_ms());
        }
    }

    void wake_up(cb_t = nullptr)
    {
        std::unique_lock lg(m_wakeup_mtx);
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

    std::atomic<bool> m_running{false};
};

} // namespace bfc

#endif // __BFC_CV_REACTOR_HPP__
