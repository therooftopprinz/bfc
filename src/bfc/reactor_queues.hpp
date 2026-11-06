#ifndef __BFC_REACTOR_QUEUES_HPP__
#define __BFC_REACTOR_QUEUES_HPP__

#include <algorithm>
#include <mutex>
#include <vector>

#include <bfc/function.hpp>

namespace bfc
{

// The reactive queues an fd reactor (poll_reactor, epoll_reactor) serves
// besides its fds, as cv_reactor does. A queue is bound with
// queue.set_reactor(reactor) (a push wakes the reactor) and registered with
// reactor.add_read_rdy(queue, cb): while the queue has data, cb runs on the
// reactor thread once per pass. When a queue still has data after its cb
// (cb took only part of it), the reactor's next wait does not block, so the
// rest is handled after the other ready work.
//
// The reactor keeps cb itself (the queue's own callback slot is left alone),
// so the queue's callback type need not match the reactor's. It holds the
// queue by reference: a queue must stay alive until it is removed
// (rem_read_rdy) or the reactor is gone.
template <typename cb_t>
class reactor_queues
{
public:
    template <typename queue_t>
    void add(queue_t& queue, cb_t cb)
    {
        std::unique_lock<std::mutex> lg(m_mtx);
        for (auto& e : m_entries)
        {
            if (e.key == &queue)
            {
                e.cb = std::move(cb);
                return;
            }
        }
        m_entries.push_back(entry_s{&queue, [q = &queue]() { return q->has_data(); }, std::move(cb)});
    }

    // From the next pass on.
    template <typename queue_t>
    void rem(queue_t& queue)
    {
        std::unique_lock<std::mutex> lg(m_mtx);
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                       [&](const entry_s& e) { return e.key == &queue; }),
                        m_entries.end());
    }

    // Reactor thread: runs the cb of every queue with data. Returns true when
    // one still has data afterwards (the next wait must not block).
    bool notify()
    {
        {
            std::unique_lock<std::mutex> lg(m_mtx);
            if (m_entries.empty())
            {
                return false;
            }
            m_snapshot.assign(m_entries.begin(), m_entries.end());
        }
        bool backlog = false;
        for (auto& e : m_snapshot)
        {
            if (e.has_data())
            {
                e.cb();
                backlog = backlog || e.has_data();
            }
        }
        m_snapshot.clear();
        return backlog;
    }

private:
    struct entry_s
    {
        const void* key;
        light_function<bool()> has_data;
        cb_t cb;
    };

    std::mutex m_mtx;
    std::vector<entry_s> m_entries;
    std::vector<entry_s> m_snapshot;  // reactor thread only
};

} // namespace bfc

#endif // __BFC_REACTOR_QUEUES_HPP__
