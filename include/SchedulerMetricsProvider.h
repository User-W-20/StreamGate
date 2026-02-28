//
// Created by wxx on 2026/2/13.
//

#ifndef STREAMGATE_SCHEDULERMETRICSPROVIDER_H
#define STREAMGATE_SCHEDULERMETRICSPROVIDER_H
#include "IMetricsProvider.h"

// 前置声明，避免头文件循环依赖
class StreamTaskScheduler;

/**
 * @brief 调度器指标提供者
 * 负责从 StreamTaskScheduler 中拉取任务调度相关的核心统计数据
 */
class SchedulerMetricsProvider final : public IMetricsProvider
{
public:
    explicit SchedulerMetricsProvider(StreamTaskScheduler* scheduler = nullptr)
        : _scheduler(scheduler)
    {
    }

    ~SchedulerMetricsProvider() override = default;

    // 使用宏定义监控项名称
    REGISTER_METRICS_NAME("scheduler_metrics")

    /**
     * @brief 注入调度器实例
     */
    void setScheduler(StreamTaskScheduler* scheduler) noexcept
    {
        _scheduler = scheduler;
    }

    /**
     * @brief 周期性刷新逻辑
     */
    void refresh() noexcept override;

private:
    StreamTaskScheduler* _scheduler; // 观察者指针
};
#endif //STREAMGATE_SCHEDULERMETRICSPROVIDER_H
