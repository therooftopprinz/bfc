#ifndef BFC_THREAD_POOL_HPP
#define BFC_THREAD_POOL_HPP

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <bfc/function.hpp>

namespace bfc
{

template <typename function_t = light_function<void()>>
class thread_pool
{
public:
    using fn_t = function_t;
    thread_pool(size_t p_max_size = 4)
    {
        std::unique_lock<std::mutex> lg(m_free_mtx);
        for (auto i = 0u; i < p_max_size; i++)
        {
            m_free.emplace_back(i);
            m_pool.emplace_back(std::make_unique<thread_entry>());
            auto& entry = *(m_pool.back());
            entry.functor = {};

            entry.thread = std::thread([this, use_index = i, &entry]()
                {
                    auto pred = [&]() { return entry.functor || !m_is_running; };
                    while (true)
                    {
                        std::unique_lock<std::mutex> lg(entry.entry_mtx);

                        if (entry.functor)
                        {
                            entry.functor();
                            entry.functor.reset();
                            lg.unlock();
                            {
                                std::unique_lock<std::mutex> lg_free(m_free_mtx);
                                m_free.emplace_back(use_index);
                            }
                            m_worker_available_cv.notify_one();
                            continue;
                        }

                        if (!m_is_running)
                        {
                            return;
                        }

                        entry.thread_cv.wait(lg, pred);
                    }
                });
        }
    }

    ~thread_pool()
    {
        m_is_running = false;
        for (auto& i : m_pool)
        {
            i->thread_cv.notify_one();
            i->thread.join();
        }
    }
    void execute(function_t p_functor)
    {
        std::unique_lock<std::mutex> lg(m_free_mtx);

        m_worker_available_cv.wait(lg, [this]() {
            return !m_free.empty();
        });

        auto use_index = m_free.back();
        m_free.pop_back();
        auto& entry = *m_pool[use_index];

        std::unique_lock<std::mutex> lgEntry(entry.entry_mtx);
        entry.functor = std::move(p_functor);
        entry.thread_cv.notify_one();
    }

    size_t count_active() const
    {
        std::unique_lock<std::mutex> lg(m_free_mtx);
        return m_pool.size() - m_free.size();
    }

    size_t size() const
    {
        return m_pool.size();
    }
private:
    struct thread_entry
    {
        function_t functor;
        std::thread thread;
        std::condition_variable thread_cv;
        std::mutex entry_mtx;
    };

    bool m_is_running = true;
    std::vector<std::unique_ptr<thread_entry>> m_pool;
    std::condition_variable m_worker_available_cv;
    std::vector<size_t> m_free;
    mutable std::mutex m_free_mtx;
};

} // namespace bfc

#endif // BFC_THREAD_POOL_HPP
