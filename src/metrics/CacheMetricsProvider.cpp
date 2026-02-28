//
// Created by wxx on 2026/2/15.
//
#include "CacheMetricsProvider.h"
#include "CacheManager.h"
#include <chrono>

REGISTER_METRICS(CacheMetricsProvider)

void CacheMetricsProvider::refresh() noexcept
{
    //哨兵检查：若 Cache 未就绪，直接返回初始状态
    if (!_cache)
    {
        updateSnapshot({
            {"status", "not_initialized"},
            {"connected", false}
        });

        return;
    }

    //测量 Probe 耗时 (网络 I/O 延迟监控)
    const auto start = std::chrono::steady_clock::now();

    /**
     * 调用 ping()。
     * 假设 CacheManager::ping() 内部已通过 try-catch 保护，
     * 若 Redis 宕机或超时，应返回 false 而非抛出异常。
     */
    const bool is_connected = _cache->ping();

    const auto end = std::chrono::steady_clock::now();
    const auto rtt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    //更新快照：使用列表初始化减少内存分配开销
    if (is_connected)
    {
        updateSnapshot({
            {"status", "connected"},
            {"connected", true},
            {"latency_ms", rtt_ms},
            {"last_check_ok", true}
        });
    }
    else
    {
        updateSnapshot({
            {"status", "disconnected"},
            {"connected", false},
            {"latency_ms", -1},
            {"last_check_ok", false}
        });
    }
}

extern "C" void ForceLink_CacheMetricsProvider()
{
}
