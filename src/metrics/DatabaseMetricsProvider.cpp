//
// Created by wxx on 2026/2/15.
//
#include "DatabaseMetricsProvider.h"
#include "DBManager.h"

REGISTER_METRICS(DatabaseMetricsProvider)

void DatabaseMetricsProvider::refresh() noexcept
{
    //哨兵检查：若驱动未注入
    if (!_db)
    {
        updateSnapshot({{"status", "not_initialized"}});
        return;
    }

    /**
      * 获取指标
      */
    auto stats = _db->getPoolStats();

    //状态判定逻辑
    const char* status_str = "disconnected";
    if (stats.is_ok)
    {
        status_str = "connected";
    }
    else if (stats.current_size > 0)
    {
        status_str = "degraded";
    }

    //更新快照
    updateSnapshot({
        {"status", status_str},
        {"is_healthy", stats.is_ok},
        {"pool_size", stats.current_size},
        {
            "pool", {
                {"pool_size", stats.current_size},
                {"active", stats.active_count},
                {"idle", stats.idle_count},
                {"waiting", stats.wait_count}
            }
        }
    });
}

extern "C" void ForceLink_DatabaseMetricsProvider()
{
}
