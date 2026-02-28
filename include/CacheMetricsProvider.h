//
// Created by wxx on 2026/2/15.
//

#ifndef STREAMGATE_CACHEMETRICSPROVIDER_H
#define STREAMGATE_CACHEMETRICSPROVIDER_H
#include "IMetricsProvider.h"

// 前向声明，保持头文件轻量
class CacheManager;

/**
 * @brief 缓存监控提供者
 * 负责监控 Redis 连接活性及 RTT（往返时延）
 */
class CacheMetricsProvider final : public IMetricsProvider
{
public:
    explicit CacheMetricsProvider(CacheManager* cache = nullptr)
        : _cache(cache)
    {
    }

    ~CacheMetricsProvider() override = default;

    // 静态监控项名称定义
    REGISTER_METRICS_NAME("cache_metrics")

    /**
     * @brief 周期性执行 Probe 操作
     */
    void refresh() noexcept override;

    /**
     * @brief 注入缓存管理实例
     */
    void setCache(CacheManager* cache) noexcept
    {
        _cache = cache;
    }

private:
    CacheManager* _cache; // 弱引用，指向全局 Cache 实例
};
#endif //STREAMGATE_CACHEMETRICSPROVIDER_H
