//
// Created by X on 2025/11/22.
//

#ifndef STREAMGATE_CACHEMANAGER_H
#define STREAMGATE_CACHEMANAGER_H
#include <sw/redis++/redis++.h>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include "StreamAuthData.h"

enum CacheResult
{
    CACHE_HIT_SUCCESS = 1,
    CACHE_HIT_FAILURE = 2,
    CACHE_MISS = -1,
    CACHE_ERROR = -2
};

class CacheManager
{
public:
    static CacheManager& instance();

    // === 初始化（必须先调用）===
    void init(const std::string& host, int port, int pool_size = 8, const std::string& password = "");

    // === 认证缓存接口（同步，无 future）===
    [[nodiscard]] int getAuthResult(const std::string& streamKey, const std::string& clientId) const;
    void setAuthResult(const std::string& streamKey, const std::string& clientId, int result) const;

    [[nodiscard]] int getAuthResultByKey(const std::string& cacheKey) const;
    void setAuthResultByKey(const std::string& cacheKey, int result) const;

    [[nodiscard]] std::optional<StreamAuthData> getAuthDataFromCache(const std::string& streamKey) const;
    void setAuthDataToCache(const StreamAuthData& data, int ttl) const;

    [[nodiscard]] std::optional<StreamAuthData> getAuthDataFromCacheByKey(const std::string& customKey) const;
    void setAuthDataToCacheByKey(const std::string& key, const StreamAuthData& data, int ttl) const;
    void setEmptyAuthDataToCache(const std::string& key, int ttl) const;

    // === 高层语义封装（全部为 const，线程安全）===
    // Hash 操作
    [[nodiscard]] bool hashSet(const std::string& key,
                               const std::unordered_map<std::string, std::string>& fields) const;
    [[nodiscard]] std::unordered_map<std::string, std::string> hashGetAll(const std::string& key) const;
    [[nodiscard]] bool hashDel(const std::string& key, const std::string& field) const;
    [[nodiscard]] bool hashKeyDel(const std::string& key) const;
    [[nodiscard]] long long hashIncrBy(const std::string& key, const std::string& field, long long increment) const;

    // Set 操作
    [[nodiscard]] bool setAdd(const std::string& key, const std::string& member) const;
    [[nodiscard]] bool setAdd(const std::string& key, const std::vector<std::string>& members) const;
    [[nodiscard]] bool setRem(const std::string& key, const std::string& member) const;
    [[nodiscard]] std::vector<std::string> setMembers(const std::string& key) const;
    [[nodiscard]] size_t setCard(const std::string& key) const;
    [[nodiscard]] bool setDel(const std::string& key) const;

    // ZSet 操作
    [[nodiscard]] bool zsetAdd(const std::string& key, double score, const std::string& member) const;
    [[nodiscard]] std::vector<std::string> zsetRangeByScore(const std::string& key, double min, double max) const;
    [[nodiscard]] bool zsetRem(const std::string& key, const std::string& member) const;

    // 通用操作
    [[nodiscard]] bool keyExpire(const std::string& key, int seconds) const;
    [[nodiscard]] bool keyDel(const std::string& key) const;
    [[nodiscard]] bool keyExists(const std::string& key) const;

    // === 状态与健康检查 ===
    [[nodiscard]] int getTTL() const
    {
        return _cacheTTL;
    }

    [[nodiscard]] bool ping() const;

    [[nodiscard]] bool isReady() const
    {
        return _io_running.load(std::memory_order_acquire);
    }

    // Delete copy/move
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    // 返回一个 redis++ 的 Pipeline 对象
    // 注意：Pipeline 对象是非线程安全的，必须在当前线程使用
    [[nodiscard]] sw::redis::Pipeline createPipeline() const
    {
        if (!_redis)
        {
            throw std::runtime_error("CacheManager not initialized");
        }
        return _redis->pipeline();
    }

private:
    CacheManager() = default;
    ~CacheManager();

    std::unique_ptr<sw::redis::Redis> _redis;
    int _cacheTTL = 300;
    std::atomic<bool> _io_running{false};

    [[nodiscard]] static std::string buildKey(const std::string& streamKey, const std::string& clientId);
    [[nodiscard]] std::optional<std::string> getString(const std::string& key) const;
    void setString(const std::string& key, const std::string& value, int ttl = -1) const;
};
#endif  // STREAMGATE_CACHEMANAGER_H
