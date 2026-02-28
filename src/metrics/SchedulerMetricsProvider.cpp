//
// Created by wxx on 2026/2/14.
//
#include "SchedulerMetricsProvider.h"
#include "StreamTaskScheduler.h"

REGISTER_METRICS(SchedulerMetricsProvider)

void SchedulerMetricsProvider::refresh() noexcept
{
    //哨兵检查：若调度器未注入或尚未初始化，更新基础状态
    if (!_scheduler)
    {
        updateSnapshot({
            {"status", "initializing"},
            {"ready", false}
        });

        return;
    }

    /**
     * 读取指标
     * 假设 StreamTaskScheduler::getMetrics() 返回的是包含原子变量或
     * 已经过一致性处理的 Snapshot 结构体。
     */
    auto m = _scheduler->getMetrics();

    /**
     * 构建快照
     * 使用初始化列表：减少键值对插入时的哈希计算与多次内存分配。
     * 计算逻辑（如 failed_pub）直接在导出时完成。
     */
    updateSnapshot({
        {"total_publish_req", m.total_publish_req},
        {"success_pub", m.success_pub},
        {"failed_pub", m.total_publish_req - m.success_pub},
        {"total_play_req", m.total_play_req},
        {"success_play", m.success_play},
        {"failed_play", m.total_play_req - m.success_play},
        {"auth_failures", m.auth_failures},
        {"tasks_cleaned", m.tasks_cleaned},
        {"timestamp_ms", m.last_update_ms}
    });
}

extern "C" void ForceLink_SchedulerMetricsProvider()
{
}
