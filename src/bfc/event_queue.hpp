#ifndef __BFC_EVENT_QUEUE_HPP__
#define __BFC_EVENT_QUEUE_HPP__

#include <cstdint>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <bfc/function.hpp>
#include <bfc/object_registry.hpp>

namespace bfc
{

// Polymorphic base for reactive queues, decoupled from T.
template <typename cb_t>
class reactive_event_queue_base
{
public:
    using callback_t = cb_t;
    virtual ~reactive_event_queue_base() = default;
    virtual void set_callback(callback_t cb) = 0;
    virtual bool has_data() = 0;
    virtual void notify_callback() = 0;
};

template <typename T, typename callback_t>
class reactive_event_queue : public reactive_event_queue_base<callback_t>
{
public:
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

// Fixed-capacity FIFO ring; when full, push drops the oldest element.
// Slots use std::optional so T need not be default-constructible.
template <typename T, typename callback_t = light_function<void()>>
class reactive_circular_lossy_queue
    : public reactive_event_queue_base<callback_t>
{
public:
    reactive_circular_lossy_queue() = default;
    ~reactive_circular_lossy_queue() = default;

    void set_capacity(size_t capacity)
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        if (capacity == m_capacity)
        {
            return;
        }

        std::vector<T> kept;
        if (m_capacity != 0 && m_count != 0)
        {
            const size_t keep = std::min(m_count, capacity);
            const size_t drop = m_count - keep;
            size_t idx = (m_head + drop) % m_capacity;
            kept.reserve(keep);
            for (size_t i = 0; i < keep; ++i)
            {
                kept.push_back(std::move(*m_slots[idx]));
                m_slots[idx].reset();
                idx = (idx + 1) % m_capacity;
            }
        }

        std::vector<std::optional<T>> new_slots(capacity);
        const size_t new_count = kept.size();
        for (size_t i = 0; i < new_count; ++i)
        {
            new_slots[i] = std::move(kept[i]);
        }

        m_slots = std::move(new_slots);
        m_capacity = capacity;
        m_head = 0;
        m_count = new_count;
    }

    // Rvalue only. Returns occupancy after the push (0 when capacity is 0).
    size_t push(T&& u)
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        if (m_capacity == 0)
        {
            return 0;
        }

        if (m_count < m_capacity)
        {
            const size_t tail = (m_head + m_count) % m_capacity;
            m_slots[tail] = std::move(u);
            ++m_count;
            return m_count;
        }

        m_slots[m_head] = std::move(u);
        m_head = (m_head + 1) % m_capacity;
        return m_count;
    }

    bool pop(T& out)
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        if (m_count == 0)
        {
            return false;
        }

        out = std::move(*m_slots[m_head]);
        m_slots[m_head].reset();
        m_head = (m_head + 1) % m_capacity;
        --m_count;
        return true;
    }

    size_t size()
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        return m_count;
    }

    size_t capacity()
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        return m_capacity;
    }

    void set_callback(callback_t cb) override
    {
        std::unique_lock<std::mutex> lg(cb_mtx);
        m_cb = std::move(cb);
    }

    bool has_data() override
    {
        std::unique_lock<std::mutex> lg(m_queue_mtx);
        return m_count != 0;
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
    std::mutex cb_mtx;
    callback_t m_cb = nullptr;
    size_t m_capacity = 0;
    size_t m_head = 0;
    size_t m_count = 0;
    std::vector<std::optional<T>> m_slots;
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

template <typename element_t = uint64_t>
using cv_reactor_event_queue_tag =
    object_tag<reactive_event_queue<element_t, light_function<void()>>>;

template <typename element_t = uint64_t>
using cv_reactor_event_queue_registry =
    object_registry<cv_reactor_event_queue_tag<element_t>>;

template <typename element_t = uint64_t>
using cv_reactor_circular_lossy_queue_tag =
    object_tag<reactive_circular_lossy_queue<element_t, light_function<void()>>>;

template <typename element_t = uint64_t>
using cv_reactor_circular_lossy_queue_registry =
    object_registry<cv_reactor_circular_lossy_queue_tag<element_t>>;

} // namespace bfc

#endif // __BFC_EVENT_QUEUE_HPP__
