/*
 * Copyright (C) 2023 Prinz Rainer Buyo <mynameisrainer@gmail.com>
 *
 * MIT License
 *
 */

#ifndef __BFC_METRIC_HPP__
#define __BFC_METRIC_HPP__

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace bfc
{

class metric
{
public:
    void store(double val)
    {
        m_value = val;
    }
    double load()
    {
        return m_value;
    }
    double fetch_add(double val)
    {
        auto old = m_value.load();

        if (old == old+val)
        {
            return old;
        }

        while(m_value.compare_exchange_strong(old, old+val));
        return old;
    }
    double fetch_sub(double val)
    {
        auto old = m_value.load();

        if (old == old-val)
        {
            return old;
        }

        while(m_value.compare_exchange_strong(old, old-val));
        return old;
    }
private:
    std::atomic<double> m_value = 0;
};

class monitor
{
public:
    monitor() = delete;

    monitor(size_t interval_ms=100, std::string path="metrics")
        : m_interval_ms(interval_ms)
        , m_path(path)
    {
        std::ofstream(m_path+".csv", std::fstream::out | std::fstream::trunc);

        m_monitor_thread = std::thread([this](){
                run_monitor();
            });
    }

    ~monitor()
    {
        m_monitor_running = false;
        if (m_monitor_thread.joinable())
        {
            m_monitor_thread.join();
        }
    }

    metric& get_metric(const std::string& full_name)
    {
        std::unique_lock lg(m_mutex);

        if (m_metrics.count(full_name))
        {
            return *m_metrics.at(full_name);
        }

        auto res = m_metrics.emplace(full_name, std::make_unique<metric>());
        return *res.first->second.get();
    }

private:
    void run_monitor()
    {
        while (m_monitor_running)
        {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch())
                .count();
            auto diff = now - m_last_update;
            if (diff > m_interval_ms)
            {
                m_last_update = now;
                collect();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void collect()
    {
        std::ofstream fout(m_path+"_", std::fstream::out | std::fstream::trunc);
        std::ofstream fout2(m_path+".csv", std::fstream::out | std::fstream::app);
        std::unique_lock lg(m_mutex);
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        fout2 << now << ",";
        for (auto& [key, val] : m_metrics)
        {
            fout.precision(20);
            fout2.precision(20);
            fout << key << " " << val->load() << "\n";
            fout2 << val->load() << ",";
        }
        fout2 << "\n";

        std::filesystem::rename(m_path+"_", m_path);
    }

    size_t m_interval_ms = 1000;
    std::string m_path = "metrics";
    int64_t m_last_update = 0;
    std::atomic_bool m_monitor_running = true;
    std::thread m_monitor_thread;
    std::mutex m_mutex;
    std::map<std::string, std::unique_ptr<metric>> m_metrics;
};

} // namespace bfc

#endif // __BFC_METRIC_HPP__
