//
// Created by wxx on 12/11/25.
//

#ifndef STREAMGATE_HYBRIDAUTHREPOSITORY_H
#define STREAMGATE_HYBRIDAUTHREPOSITORY_H
#include <string>
#include <optional>
#include <atomic>
#include <string_view>
#include "CacheManager.h"
#include "DBManager.h"
#include "IAuthRepository.h"
#include "StreamAuthData.h"

class HybridAuthRepository : public IAuthRepository
{
public:
    struct Stats
    {
        uint64_t cache_hits;
        uint64_t cache_misses;
        uint64_t db_hits;
        uint64_t db_misses;
        uint64_t db_errors;
        uint64_t validation_failures; // 逻辑校验失败次数（Token/ClientId 不匹配，或 DB 数据校验失败）
        double cache_hit_rate; // 物理缓存命中率
    };

    HybridAuthRepository(DBManager& dbManager, CacheManager& cacheManager);

    // 核心业务接口
    std::optional<StreamAuthData> getAuthData(const std::string& streamKey,
                                              const std::string& clientId,
                                              const std::string& authToken) override;

    // 运维接口
    [[nodiscard]] Stats getStats() const;
    void resetStats();
    bool isHealthy() override;

private:
    // 内部逻辑拆分
    static std::string buildCacheKey(const std::string& streamKey, const std::string& clientId);

    std::optional<StreamAuthData> getAuthDataFromDB(const std::string& streamKey,
                                                    const std::string& clientId,
                                                    const std::string& authToken) const;

    //增加透明化日志
    std::optional<StreamAuthData> tryGetFromCache(const std::string& cacheKey) const;
    void cacheAuthData(const std::string& cacheKey, const StreamAuthData& data) const;
    void cacheNegativeResult(const std::string& cacheKey, int ttl) const;

    //增加 cacheKey 参数，避免重复计算
    std::optional<StreamAuthData> queryDatabase(const std::string& streamKey,
                                                const std::string& clientId,
                                                const std::string& authToken,
                                                const std::string& cacheKey);

    //参数使用 std::string_view
    static bool validateAuthData(const StreamAuthData& data,
                                 std::string_view expectedStreamKey,
                                 std::string_view expectedClientId,
                                 std::string_view expectedToken);

private:
    DBManager& _dbManager;
    CacheManager& _cacheManager;

    const int _cacheTTL;
    static constexpr int NEGATIVE_CACHE_TTL = 30; // 记录不存在时的负缓存TTL
    static constexpr int TRANSIENT_DB_ERROR_TTL = 5; // DB异常时的短期负缓存TTL (Anti-Collapse)

    // 统计指标（原子变量确保线程安全）
    std::atomic<uint64_t> _cacheHits{0};
    std::atomic<uint64_t> _cacheMisses{0};
    std::atomic<uint64_t> _dbHits{0};
    std::atomic<uint64_t> _dbMisses{0};
    std::atomic<uint64_t> _dbErrors{0};
    std::atomic<uint64_t> _validationFailures{0};
};

#endif //STREAMGATE_HYBRIDAUTHREPOSITORY_H
