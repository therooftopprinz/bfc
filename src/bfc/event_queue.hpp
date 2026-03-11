#ifndef __BFC_EVENT_QUEUE_HPP__
#define __BFC_EVENT_QUEUE_HPP__

#include <condition_variable>
#include <mutex>
#include <vector>

#include <bfc/function.hpp>

namespace bfc
{

// Polymorphic base for reactive queues, decoupled from T.
template <typename cb_t = light_function<void()>>
class reactive_event_queue_base
{
public:
    using callback_t = cb_t;
    virtual ~reactive_event_queue_base() = default;
    virtual void set_callback(callback_t cb) = 0;
    virtual bool has_data() = 0;
    virtual void notify_callback() = 0;
};

template <typename T>
class reactive_event_queue : public reactive_event_queue_base<>
{
public:
    using callback_t = typename reactive_event_queue_base<>::callback_t;

    reactive_event_queue() = default;

    ~reactive_event_queue() = default;

    template <typename U>
    size_t push(U&& u)
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        m_queue.emplace_back(std::forward<U>(u));
        return m_queue.size();
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

    void set_callback(callback_t cb) override
    {
        std::unique_lock<std::mutex> lg(cb_mtx);
        m_cb = std::move(cb);
    }

    bool has_data() override
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        return !m_queue.empty();
    }

    void notify_callback() override
    {
        std::unique_lock<std::mutex> lg(cb_mtx);
        if (m_cb)
        {
            m_cb();
        }
    }

private:
    std::mutex m_queue_mtx;
    std::vector<T> m_queue;
    std::mutex cb_mtx;
    callback_t m_cb = nullptr;
};

template <typename T>
class event_queue
{
public:
    explicit event_queue(bool blocking = true)
        : m_blocking(blocking)
    {}

    ~event_queue() = default;

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
        if (m_blocking && m_queue.empty())
        {
            cv.wait(lg);
        }
        return std::move(m_queue);
    }

    size_t size()
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        return m_queue.size();
    }

    void wake_up()
    {
        if (!m_blocking) return;
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
