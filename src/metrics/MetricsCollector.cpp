//
// Created by wxx on 2026/2/4.
//
#include "MetricsCollector.h"
#include "Logger.h"
#include <chrono>
#include <format>
#include <algorithm>
#include <iostream>

void MetricsCollector::registerProvider(const std::shared_ptr<IMetricsProvider>& provider)
{
    if (!provider)return;

    std::unique_lock lock(_mutex);
    _providers.push_back(provider);

    //确保 JSON 输出顺序永远一致 (Diff-Friendly)
    std::ranges::sort(_providers, [](const auto& a, const auto& b)
    {
        return a->metricsName() < b->metricsName();
    });

    std::string log_msg = std::format("[Metrics] Registered provider: {}", provider->metricsName());
    LOG_INFO(log_msg);
}

nlohmann::json MetricsCollector::collectAll() const
{
    nlohmann::json root;

    // 记录全局时间戳
    auto now = std::chrono::system_clock::now();
    root["timestamp"] = std::format("{:%Y-%m-%d %H:%M:%S}", floor<std::chrono::seconds>(now));

    root["components"] = nlohmann::json::object();

    {
        std::shared_lock lock(_mutex);
        for (const auto& p : _providers)
        {
            auto start = std::chrono::steady_clock::now();

            root["components"][std::string(p->metricsName())] = p->exportMetrics();

            auto end = std::chrono::steady_clock::now();
            auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

            if (cost > kSlowThreshold)
            {
                std::string warn_msg = std::format("[Metrics] Provider [{}] is SLOW! Cost: {} us", p->metricsName(),
                                                   cost);
                LOG_WARN(warn_msg);
            }
        }
    }
    return root;
}

MetricsCollector& MetricsCollector::instance()
{
    static MetricsCollector inst;
    return inst;
}

void MetricsCollector::start(std::chrono::milliseconds interval,
                             std::move_only_function<void(const nlohmann::json&) const> exporter)
{
    //防止重复启动
    if (_worker.joinable())return;

    _worker = std::jthread([this,interval,exp=std::move(exporter)](const std::stop_token& st)
    {
        LOG_INFO("MetricsCollector: Background worker started.");

        // 使用 steady_clock 防止系统调时导致频率错乱
        auto next_tick = std::chrono::steady_clock::now();

        while (!st.stop_requested())
        {
            {
                std::shared_lock lock(_mutex);
                for (auto &p:_providers)
                {
                    p->refresh();
                }

            }
            // --- A. 执行任务 ---
            nlohmann::json report = collectAll();
            if (exp)
            {
                exp(report);
            }

            // --- B. 计算下一次触发点 (零漂移) ---
            next_tick += interval;

            // --- C. 可中断的精准休眠 ---
            std::unique_lock<std::mutex> lock(_cv_m);
            _cv.wait_until(lock, st, next_tick, [] { return false; });
        }
        LOG_INFO("MetricsCollector: Background worker stopped.");
    });
}

void MetricsCollector::stop()
{
    if (_worker.joinable())
    {
        //触发 stop_token，让 st.stop_requested() 变为 true
        _worker.request_stop();

        //唤醒正在 wait_until 的条件变量，让线程别等 next_tick 了，立刻醒来
        _cv.notify_all();

        //等待线程彻底安全退出
        _worker.join();
    }
}
