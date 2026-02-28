//
// Created by wxx on 2026/2/21.
//

#ifndef STREAMGATE_HEALTHCHECKER_H
#define STREAMGATE_HEALTHCHECKER_H
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// 前向声明
class CacheManager;
class DBManager;
class StreamTaskScheduler;

class HealthChecker
{
public:
    /**
     * @brief 组件健康状态枚举
     */
    enum class HealthStatus
    {
        HEALTHY, // 正常
        DEGRADED, // 降级（存在延迟或资源背压）
        UNHEALTHY // 异常（断连或宕机）
    };

    /**
     * @brief 单个组件的精细化指标
     */
    struct ComponentHealth
    {
        std::string name;
        HealthStatus status;
        std::string details;
        int64_t response_time_ms{0};
    };

    /**
     * @brief 整体健康快照
     */
    struct HealthReport
    {
        HealthStatus overall_status;
        std::vector<ComponentHealth> components;
        std::string timestamp;
        int score{0};
    };

    /**
     * @brief 构造函数
     */
    HealthChecker(
        CacheManager* cache = nullptr,
        DBManager* db = nullptr,
        StreamTaskScheduler* scheduler = nullptr
    );

    // 禁用拷贝与移动
    HealthChecker(const HealthChecker&) = delete;
    HealthChecker& operator=(const HealthChecker&) = delete;

    /**
     * @brief 执行深度健康检查
     * @return 包含所有组件状态的报告
     */
    HealthReport check();

    /**
     * @brief 转换为符合云原生标准的 JSON 格式
     */
    static nlohmann::json toJson(const HealthReport& report);

private:
    [[nodiscard]] ComponentHealth checkRedis() const;
    [[nodiscard]] ComponentHealth checkDatabase() const;
    [[nodiscard]] ComponentHealth checkScheduler() const;

    static HealthStatus calculateOverallStatus(const std::vector<ComponentHealth>& components);
    static int calculateHealthScore(const std::vector<ComponentHealth>& components);

    static std::string statusToString(HealthStatus status);

    // 弱引用各管理组件
    CacheManager* _cache;
    DBManager* _db;
    StreamTaskScheduler* _scheduler;
};
#endif //STREAMGATE_HEALTHCHECKER_H
