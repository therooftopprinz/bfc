#ifndef __BFC_EVENT_QUEUE_HPP__
#define __BFC_EVENT_QUEUE_HPP__

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <memory>
#include <optional>
#include <type_traits>
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

    // Retains the reactor without making the queue type depend on its type.
    // Binding is independent of read callback registration. An in-flight push
    // may still notify the previous reactor after replacement or clearing.
    template <typename reactor_t>
    void set_reactor(std::shared_ptr<reactor_t> reactor)
    {
        light_function<void()> wake_callback;
        if (reactor)
        {
            wake_callback = [reactor = std::move(reactor)]() {
                reactor->wake_up();
            };
        }
        replace_wake_callback(std::move(wake_callback));
    }

    void set_reactor(std::nullptr_t)
    {
        replace_wake_callback(nullptr);
    }

protected:
    // Copy under the binding mutex, then invoke with no queue locks held.
    // The copy retains the reactor until this notification completes.
    void notify_push()
    {
        auto wake_callback = [this]() {
            std::unique_lock<std::mutex> lock(m_reactor_mtx);
            return m_wake_callback;
        }();
        if (wake_callback)
        {
            wake_callback();
        }
    }

private:
    void replace_wake_callback(light_function<void()> wake_callback)
    {
        light_function<void()> retired;
        {
            std::unique_lock<std::mutex> lock(m_reactor_mtx);
            retired = std::move(m_wake_callback);
            m_wake_callback = std::move(wake_callback);
        }
        // Release the previous reactor outside the binding mutex.
    }

    std::mutex m_reactor_mtx;
    light_function<void()> m_wake_callback;
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
        size_t size_after;
        {
            std::unique_lock<std::mutex> lg(m_queue_mtx);
            m_queue.emplace_back(std::forward<U>(u));
            size_after = m_queue.size();
        }
        this->notify_push();
        return size_after;
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

// Fixed-capacity FIFO ring. push drops the oldest element when the ring is full.
// T must be nothrow-move-constructible. It need not be default-constructible
// or move-assignable. Slots hold std::optional<T> so an empty slot has no T.
// cv_reactor invokes the read callback once per wake. That callback has to
// drain via pop(); elements left in the ring wait for a later wake.
// With set_reactor(), every enqueue (including overwrite) signals the reactor.
// Capacity-zero pushes discard the item and do not signal.
// Destructors of dropped and retired elements run after the queue mutex is
// released.
template <typename T, typename callback_t = light_function<void()>>
class reactive_circular_lossy_queue
    : public reactive_event_queue_base<callback_t>
{
public:
    static_assert(std::is_nothrow_move_constructible<T>::value,
        "reactive_circular_lossy_queue requires nothrow move construction");

    reactive_circular_lossy_queue() = default;
    ~reactive_circular_lossy_queue() = default;

    // Keeps the newest min(size, capacity) elements, in order. Allocation
    // happens before any slot is moved, so a failed allocation leaves the
    // ring unchanged.
    void set_capacity(size_t capacity)
    {
        std::vector<std::optional<T>> retired;
        {
            std::unique_lock<std::mutex> lg(m_queue_mtx);
            if (capacity == m_capacity)
            {
                return;
            }

            std::vector<std::optional<T>> new_slots(capacity);
            const size_t keep = (m_capacity == 0) ? 0 : std::min(m_count, capacity);
            if (keep != 0)
            {
                const size_t drop = m_count - keep;
                size_t idx = (m_head + drop) % m_capacity;
                for (size_t i = 0; i < keep; ++i)
                {
                    new_slots[i].emplace(std::move(*m_slots[idx]));
                    idx = (idx + 1) % m_capacity;
                }
            }

            retired.swap(m_slots);
            m_slots.swap(new_slots);
            m_capacity = capacity;
            m_head = 0;
            m_count = keep;
        }
    }

    // Rvalue only. Takes ownership of u, including when capacity is 0.
    // Returns the occupancy before this call. A non-zero capacity and a
    // return value equal to that capacity means this call dropped the oldest
    // element.
    size_t push(T&& u)
    {
        std::optional<T> dropped;
        size_t size_before = 0;
        bool enqueued = false;
        {
            std::unique_lock<std::mutex> lg(m_queue_mtx);
            size_before = m_count;
            if (m_capacity == 0)
            {
                dropped.emplace(std::move(u));
            }
            else if (m_count < m_capacity)
            {
                enqueued = true;
                const size_t tail = (m_head + m_count) % m_capacity;
                m_slots[tail].emplace(std::move(u));
                ++m_count;
            }
            else
            {
                enqueued = true;
                dropped.emplace(std::move(*m_slots[m_head]));
                m_slots[m_head].emplace(std::move(u));
                m_head = (m_head + 1) % m_capacity;
            }
        }
        if (enqueued)
        {
            this->notify_push();
        }
        return size_before;
    }

    // nullopt when empty. ~T of the removed element runs after the queue
    // mutex is released.
    std::optional<T> pop()
    {
        std::optional<T> out;
        {
            std::unique_lock<std::mutex> lg(m_queue_mtx);
            if (m_count == 0)
            {
                return std::nullopt;
            }

            out.emplace(std::move(*m_slots[m_head]));
            m_slots[m_head].reset();
            m_head = (m_head + 1) % m_capacity;
            --m_count;
        }
        return out;
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

    // Copies the callback and invokes it without cb_mtx held, so the handler
    // may call set_callback.
    void notify_callback() override
    {
        callback_t cb;
        {
            std::unique_lock<std::mutex> lg(cb_mtx);
            cb = m_cb;
        }
        if (cb)
        {
            cb();
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
