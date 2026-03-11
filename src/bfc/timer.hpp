#ifndef __BFC_TIMER_HPP__
#define __BFC_TIMER_HPP__

#include <map>
#include <list>
#include <chrono>
#include <mutex>

#include <bfc/function.hpp>

namespace bfc
{

template <typename cb_t = light_function<void()>>
class timer
{
public:
    using timer_id_t = std::pair<int64_t, uint64_t>;

    // Wait for a number of microseconds before firing the callback.
    timer_id_t wait_us(int64_t for_us, cb_t cb,
        int64_t now_us = current_time_us())
    {
        std::unique_lock lg(m_cb_map_mtx);
        auto next_us = now_us + for_us;
        auto timer_id = m_timer_ctr++;
        timer_id_t rv{next_us, timer_id};
        m_cb_map.emplace(rv, cb);
        return rv;
    }

    // Convenience wrapper for millisecond waits built on top of microseconds.
    timer_id_t wait_ms(int64_t for_ms, cb_t cb,
        int64_t now_us = current_time_us())
    {
        return wait_us(for_ms * 1000, std::move(cb), now_us);
    }

    bool get_next_deadline_us(int64_t& deadline_us) const
    {
        std::unique_lock lg(m_cb_map_mtx);
        if (m_cb_map.empty())
        {
            return false;
        }
        deadline_us = m_cb_map.begin()->first.first;
        return true;
    }

    // Backwards-compatible alias (now returns microsecond deadline)
    bool get_next_deadline_ms(int64_t& deadline_ms) const
    {
        return get_next_deadline_us(deadline_ms);
    }

    bool cancel(timer_id_t id)
    {
        std::unique_lock lg(m_cb_map_mtx);
        return m_cb_map.erase(id) != 0;
    }

    void schedule(int64_t now_us = current_time_us())
    {
        using node_type = typename std::map<timer_id_t, cb_t>::node_type;
        std::list<node_type> extracted;
        {
            std::unique_lock lg(m_cb_map_mtx);
            auto it = m_cb_map.begin();
            while (it != m_cb_map.end())
            {
                auto next = it;
                next++;
                auto& timer = *it;
                if (now_us >= timer.first.first)
                {
                    extracted.emplace_back(m_cb_map.extract(it));
                    it = next;
                    continue;
                }
                break;
            }
        }

        for (auto& cb : extracted)
        {
            cb.mapped()();
        }
    }

    static int64_t current_time_us()
    {
        using namespace std::chrono;
        return duration_cast<microseconds>(
            steady_clock::now().time_since_epoch()).count();
    }

    // Backwards-compatible alias
    static int64_t current_time_ms()
    {
        return current_time_us();
    }

private:
    uint64_t m_timer_ctr = 0;
    std::map<timer_id_t, cb_t> m_cb_map;
    mutable std::mutex m_cb_map_mtx;
};

} // namespace bfc

#endif // __BFC_TIMER_HPP__
