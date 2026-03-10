#ifndef __BFC_EVENT_QUEUE_HPP__
#define __BFC_EVENT_QUEUE_HPP__

#include <condition_variable>
#include <atomic>
#include <mutex>
#include <deque>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/unistd.h>

#include <bfc/function.hpp>

namespace bfc
{

template <typename T, typename reactor_t>
class reactive_event_queue
{
public:
    reactive_event_queue(reactor_t* reactor = nullptr)
        : m_reactor (reactor)
    {}

    ~reactive_event_queue()
    {}

    template <typename U>
    size_t push(U&& u)
    {
        size_t rv = 0;
        {
            std::unique_lock<std::mutex> lg(m_queue_mtx);
            m_queue.emplace_back(std::forward<U>(u));
            rv = m_queue.size();
        }

        if (m_reactor)
        {
            m_reactor->wake_up();
        }

        return rv;
    }

    std::vector<T> pop()
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        return std::move(m_queue);
    }

    size_t size()
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        return m_queue.size();
    }

private:
    template <typename, typename> friend class cv_reactor;

    std::mutex m_queue_mtx;
    std::vector<T> m_queue;
    std::mutex cb_mtx;
    light_function<void()> cb = nullptr;
    reactor_t* m_reactor = nullptr;
};

template <typename T>
class event_queue
{
public:
    event_queue(bool blocking = true)
        : m_blocking(blocking)
    {}

    ~event_queue()
    {}

    template <typename U>
    size_t push(U&& u)
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        m_queue.emplace_back(std::forward<U>(u));
        wake_up();
        return m_queue.size();
    }

    std::vector<T> pop()
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        if (m_blocking && 0 == m_queue.size()) cv.wait(lg);
        return std::move(m_queue);
    }

    size_t size()
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        return m_queue.size();
    }

    void wake_up()
    {
        if (!m_blocking)
        {
            return;
        }
        cv.notify_one();
    }

private:
    bool m_blocking = true;
    std::mutex m_queue_mtx;
    std::condition_variable cv;
    std::vector<T> m_queue;
};

} // namespace bfc

#endif // __BFC_EVENT_QUEUE_HPP__
