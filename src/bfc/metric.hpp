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
#include <sstream>
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
    monitor()  = default;
    ~monitor() = default;

    std::shared_ptr<metric> get_metric(const std::string& name)
    {
        std::unique_lock lg(m_mutex);

        if (m_metrics.count(name))
        {
            return m_metrics.at(name);
        }

        auto res = m_metrics.emplace(name, std::make_shared<metric>());
        return res.first->second;
    }

    std::string collect() const
    {
        std::stringstream ss;
        std::unique_lock lg(m_mutex);
        for (auto& [key, val ] : m_metrics)
        {
            ss << key << " " << val->load() << "\n";
        }
        return ss.str();
    }

private:
    mutable std::mutex m_mutex;
    std::map<std::string, std::shared_ptr<metric>> m_metrics;
};

} // namespace bfc

#endif // __BFC_METRIC_HPP__
