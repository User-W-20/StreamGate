//
// Created by wxx on 2026/2/4.
//

#ifndef STREAMGATE_METRICSCOLLECTOR_H
#define STREAMGATE_METRICSCOLLECTOR_H
#include "IMetricsProvider.h"
#include <vector>
#include <memory>
#include <shared_mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <condition_variable>
#include <functional>

/**
 * @brief MetricsCollector - 监控指标汇总器
 * 负责管理所有 IMetricsProvider，并生成全局监控报告。
 */
class MetricsCollector
{
public:
    // 限制单次收集的最大耗时阈值（500微秒）
    static constexpr std::chrono::microseconds kSlowThreshold{500};

    /**
     * @brief 注册一个指标提供者
     * 注册过程会自动按 metricsName 排序，确保输出 JSON 结构稳定。
     */
    void registerProvider(const std::shared_ptr<IMetricsProvider>& provider);

    /**
     * @brief 收集所有组件指标并汇总
     * @return 包含所有组件快照的 JSON 对象
     */
    [[nodiscard]] nlohmann::json collectAll() const;

    /**
     * @brief 获取单例（如果需要全局访问）
     */
    static MetricsCollector& instance();

    /**
     * @brief 启动定时导出线程
     */
    void start(std::chrono::milliseconds interval, std::move_only_function<void(const nlohmann::json&) const> exporter);

    /**
     * @brief 停止导出线程
     */
    void stop();

    ~MetricsCollector() { stop(); };

private:
    MetricsCollector() = default;

    // 使用 shared_mutex 保证注册时安全，收集时高并发读取
    mutable std::shared_mutex _mutex;
    std::vector<std::shared_ptr<IMetricsProvider>> _providers;

    std::jthread _worker;
    std::condition_variable_any _cv;
    std::mutex _cv_m;
};

#endif //STREAMGATE_METRICSCOLLECTOR_H
