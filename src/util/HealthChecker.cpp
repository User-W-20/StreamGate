//
// Created by wxx on 2026/2/21.
//
#include "HealthChecker.h"
#include "CacheManager.h"
#include "DBManager.h"
#include "StreamTaskScheduler.h"
#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

HealthChecker::HealthChecker(CacheManager* cache, DBManager* db, StreamTaskScheduler* scheduler)
    : _cache(cache),
      _db(db),
      _scheduler(scheduler)
{
    LOG_INFO("[HealthChecker] Initialized with core components");
}

std::string HealthChecker::statusToString(HealthStatus status)
{
    switch (status)
    {
    case HealthStatus::HEALTHY: return "healthy";
    case HealthStatus::DEGRADED: return "degraded";
    case HealthStatus::UNHEALTHY: return "unhealthy";
    default: return "unknown";
    }
}

HealthChecker::ComponentHealth HealthChecker::checkRedis() const
{
    ComponentHealth health{"redis", HealthStatus::UNHEALTHY, "uninitialized", 0};

    if (!_cache)return health;

    auto start = std::chrono::steady_clock::now();
    try
    {
        bool connected = _cache->ping();
        auto end = std::chrono::steady_clock::now();
        health.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        if (!connected)
        {
            health.status = HealthStatus::UNHEALTHY;
            health.details = "redis connection lost";
        }
        else if (health.response_time_ms > 50)
        {
            // 真实降级逻辑：RTT 超过 50ms 认为负载过高
            health.status = HealthStatus::DEGRADED;
            health.details = "high latency";
        }
        else
        {
            health.status = HealthStatus::HEALTHY;
            health.details = "ok";
        }
    }
    catch (const std::exception& e)
    {
        health.status = HealthStatus::UNHEALTHY;
        health.details = std::string("exception: ") + e.what();
    }
    return health;
}

HealthChecker::ComponentHealth HealthChecker::checkDatabase() const
{
    ComponentHealth health{"database", HealthStatus::UNHEALTHY, "uninitialized", 0};

    if (!_db) return health;

    auto start = std::chrono::steady_clock::now();
    try
    {
        auto stats = _db->getPoolStats();
        auto end = std::chrono::steady_clock::now();
        health.response_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        if (!stats.is_ok)
        {
            health.status = HealthStatus::UNHEALTHY;
            health.details = "db self-check failed";
        }
        else if (stats.wait_count > 0 || stats.idle_count == 0)
        {
            // 真实降级逻辑：连接池耗尽或存在等待线程（背压）
            health.status = HealthStatus::DEGRADED;
            health.details = "pool pressure (waiting: " + std::to_string(stats.wait_count) + ")";
        }
        else
        {
            health.status = HealthStatus::HEALTHY;
            health.details = "pool usage: " + std::to_string(stats.active_count) + "/" + std::to_string(
                stats.current_size);
        }
    }
    catch (const std::exception& e)
    {
        health.status = HealthStatus::UNHEALTHY;
        health.details = std::string("exception: ") + e.what();
    }
    return health;
}

HealthChecker::ComponentHealth HealthChecker::checkScheduler() const
{
    ComponentHealth health{"scheduler", HealthStatus::HEALTHY, "active", 0};

    if (!_scheduler)
    {
        health.status = HealthStatus::UNHEALTHY;
        health.details = "uninitialized";
        return health;
    }

    try
    {
        auto metrics = _scheduler->getMetrics();
        // 简单活跃判定
        uint64_t total = metrics.total_publish_req + metrics.total_play_req;
        health.details = "processed_requests: " + std::to_string(total);
    }
    catch (...)
    {
        health.status = HealthStatus::UNHEALTHY;
        health.details = "internal error";
    }
    return health;
}

HealthChecker::HealthStatus HealthChecker::calculateOverallStatus(const std::vector<ComponentHealth>& components)
{
    bool has_degraded = false;
    for (const auto& comp : components)
    {
        if (comp.status == HealthStatus::UNHEALTHY)return HealthStatus::UNHEALTHY;
        if (comp.status == HealthStatus::DEGRADED)has_degraded = true;
    }
    return has_degraded ? HealthStatus::DEGRADED : HealthStatus::HEALTHY;
}

int HealthChecker::calculateHealthScore(const std::vector<ComponentHealth>& components)
{
    if (components.empty())return 0;
    int total_score = 0;
    for (const auto& comp : components)
    {
        if (comp.status == HealthStatus::HEALTHY)total_score += 100;
        else if (comp.status == HealthStatus::DEGRADED)total_score += 50;
    }

    return total_score / static_cast<int>(components.size());
}

HealthChecker::HealthReport HealthChecker::check()
{
    HealthReport report;

    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now_t);
#else
    gmtime_r(&now_t, &tm);
#endif

    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    report.timestamp = ss.str();

    // 聚合各组件检查
    report.components.push_back(checkRedis());
    report.components.push_back(checkDatabase());
    report.components.push_back(checkScheduler());

    // 计算整体状态
    report.overall_status = calculateOverallStatus(report.components);
    report.score = calculateHealthScore(report.components);

    return report;
}

nlohmann::json HealthChecker::toJson(const HealthReport& report)
{
    json j = {
        {"status", statusToString(report.overall_status)},
        {"timestamp", report.timestamp},
        {"score", report.score},
        {"components", json::object()}
    };

    for (const auto& comp : report.components)
    {
        j["components"][comp.name] = {
            {"status", statusToString(comp.status)},
            {"details", comp.details}
        };

        if (comp.response_time_ms > 0)
        {
            j["components"][comp.name]["latency_ms"] = comp.response_time_ms;
        }
    }
    return j;
}
