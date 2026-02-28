//
// Created by wxx on 2026/2/6.
//

#ifndef STREAMGATE_SERVERMETRICSPROVIDER_H
#define STREAMGATE_SERVERMETRICSPROVIDER_H
#include "IMetricsProvider.h"
#include <array>
#include <atomic>
#include <optional>

/**
 * @brief 强一致性统计槽位
 */
struct alignas(64) WorkerStatsSlot
{
    std::atomic<uint64_t> seq{0};
    uint64_t total_requests{0};
    uint64_t success_requests{0};
    uint64_t failed_requests{0};

    void update(uint64_t t, uint64_t s, uint64_t f) noexcept;
    void read(uint64_t& t, uint64_t& s, uint64_t& f) const noexcept;

private:
    //偏执校验：确保不同平台下的布局严格锁定为一个 Cacheline
    [[maybe_unused]] std::byte _padding[64 - 32]{};
    static_assert(sizeof(std::atomic<uint64_t>) == 8);
};

static_assert(sizeof(WorkerStatsSlot) == 64, "WorkerStatsSlot must be exactly 64 bytes to prevent false sharing");

/**
 * @brief 线程局部注册表
 */
class ThreadLocalRegistry
{
public:
    static constexpr size_t kMaxWorkers = 128;
    static ThreadLocalRegistry& instance();

    std::optional<size_t> acquireSlot();
    void releaseSlot(size_t id);
    void aggregate(uint64_t& t, uint64_t& s, uint64_t& f) const noexcept;

private:
    ThreadLocalRegistry() = default;
    alignas(64) std::array<WorkerStatsSlot, kMaxWorkers> _slots;
    alignas(64) std::atomic<uint64_t> _active_mask[2]{0, 0};
    WorkerStatsSlot _historical_stats;
};

class ServerMetricsProvider final : public IMetricsProvider
{
public:
    ServerMetricsProvider() = default;

    REGISTER_METRICS_NAME("server_metrics")

    void refresh() noexcept override;
};
#endif //STREAMGATE_SERVERMETRICSPROVIDER_H
