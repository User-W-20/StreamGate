//
// Created by wxx on 2026/2/15.
//

#ifndef STREAMGATE_DATABASEMETRICSPROVIDER_H
#define STREAMGATE_DATABASEMETRICSPROVIDER_H
#include "IMetricsProvider.h"

// 前向声明，解耦 DBManager 具体的头文件
class DBManager;

/**
 * @brief 数据库监控提供者
 * 实时导出数据库连接池的健康状态与负载指标
 */
class DatabaseMetricsProvider final : public IMetricsProvider
{
public:
    explicit DatabaseMetricsProvider(DBManager* db = nullptr)
        : _db(db)
    {
    }

    ~DatabaseMetricsProvider() override = default;

    // 定义监控项标识
    REGISTER_METRICS_NAME("database_metrics")

    /**
     * @brief 周期性刷新连接池状态快照
     */
    void refresh() noexcept override;

    /**
     * @brief 注入数据库管理实例
     */
    void setDB(DBManager* db) noexcept
    {
        _db = db;
    }

private:
    DBManager* _db;
};
#endif //STREAMGATE_DATABASEMETRICSPROVIDER_H
