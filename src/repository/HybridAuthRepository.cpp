//
// Created by wxx on 12/12/25.
//
#include "HybridAuthRepository.h"
#include "Logger.h"

namespace
{
    // 敏感信息打码
    std::string maskToken(std::string_view token)
    {
        if (token.empty())return "<empty>";
        if (token.size() <= 8) return "****";
        return std::string(token.substr(0, 4)) + "..." + std::string(token.substr(token.size() - 4));
    }

    //尽力而为执行
    inline void bestEffort(bool ok, std::string_view key, std::string_view op = "InvalidateStaleCache")
    {
        if (__glibc_likely(ok))
        {
            return;
        }

        // 以下是失败路径 (Unlikely Path)
        // 只有在失败时才执行字符串拼接，最大程度保护主流程性能
        std::string msg;
        msg.reserve(op.size() + key.size() + 20);
        msg.append(op).append(" failed for key=").append(key);

        LOG_WARN(msg);
    }
}

HybridAuthRepository::HybridAuthRepository(DBManager& dbManager, CacheManager& cacheManager)
    : _dbManager(dbManager),
      _cacheManager(cacheManager),
      _cacheTTL(cacheManager.getTTL())
{
    LOG_INFO("[HybridAuthRepository] Initialized | CacheTTL: " + std::to_string(_cacheTTL) + "s");
}

std::optional<StreamAuthData> HybridAuthRepository::getAuthData(const std::string& streamKey,
                                                                const std::string& clientId,
                                                                const std::string& authToken)
{
    LOG_INFO("[HybridAuthRepository] Request: stream=" + streamKey +
        ", client=" + clientId + ", token=" + maskToken(authToken));

    //计算一次 cacheKey
    const std::string cacheKey = buildCacheKey(streamKey, clientId);

    // Step 1: Cache Path (修正统计口径)
    if (auto cacheData = tryGetFromCache(cacheKey))
    {
        ++_cacheHits; // 只要缓存里有，就是物理命中

        if (cacheData->authToken == authToken && cacheData->clientId == clientId)
        {
            LOG_DEBUG("[HybridAuthRepository] Cache HIT | Key: " + cacheKey);
            return cacheData;
        }

        // 逻辑不匹配：Token 错或 ClientId 错
        ++_validationFailures;
        LOG_WARN("[HybridAuthRepository] Cache validation mismatch | Stream: " + streamKey);
        bestEffort(_cacheManager.keyDel(cacheKey), cacheKey);
        return std::nullopt;
    }

    ++_cacheMisses;

    // Step 2: DB Path (增加熔断式负缓存)
    auto dbResult = queryDatabase(streamKey, clientId, authToken, cacheKey);

    if (!dbResult)
    {
        return std::nullopt;
    }

    // Step 3: Strict Validation (修正安全 Bug：验证所有字段)
    if (!validateAuthData(*dbResult, streamKey, clientId, authToken))
    {
        ++_validationFailures;
        return std::nullopt;
    }

    // Step 4: Success Cleanup & Cache
    LOG_INFO("[HybridAuthRepository] DB Success | Stream: " + streamKey + " | Client: " + clientId);
    cacheAuthData(cacheKey, *dbResult);

    return dbResult;
}

// 数据库查询逻辑：区分“记录不存在”与“服务异常”
std::optional<StreamAuthData> HybridAuthRepository::queryDatabase(const std::string& streamKey,
                                                                  const std::string& clientId,
                                                                  const std::string& authToken,
                                                                  const std::string& cacheKey)
{
    try
    {
        auto dbResult = getAuthDataFromDB(streamKey, clientId, authToken);

        if (dbResult.has_value())
        {
            ++_dbHits;
            return dbResult;
        }

        ++_dbMisses;
        LOG_WARN("[HybridAuthRepository] Identity not found in DB | Stream: " + streamKey);
        cacheNegativeResult(cacheKey, NEGATIVE_CACHE_TTL);
        return std::nullopt;
    }
    catch (const std::exception& e)
    {
        ++_dbErrors;
        LOG_ERROR("[HybridAuthRepository] DB Error: " + std::string(e.what()));

        //抗雪崩策略：DB 挂了也写极短负缓存，保护 DB 不被瞬时打死
        cacheNegativeResult(cacheKey, TRANSIENT_DB_ERROR_TTL); // 使用短TTL
        return std::nullopt;
    }
}

std::optional<StreamAuthData> HybridAuthRepository::getAuthDataFromDB(const std::string& streamKey,
                                                                      const std::string& clientId,
                                                                      const std::string& authToken) const
{
    //获取连接 (使用 RAII Guard)
    ConnectionGuard conn(_dbManager);
    if (!conn)
    {
        LOG_WARN("[AuthRepo] DB连接获取失败 (可能池已满或DB宕机): stream=" + streamKey);
        return std::nullopt;
    }

    const std::string sql = "SELECT client_id, is_active FROM stream_auth "
        "WHERE stream_key = ? AND client_id = ? AND auth_token = ? LIMIT 1";

    try
    {
        auto pstmt = conn->prepareStatement(sql);
        pstmt->setString(1, streamKey);
        pstmt->setString(2, clientId);
        pstmt->setString(3, authToken);

        const auto res = pstmt->executeQuery();
        if (!res->next())
        {
            return std::nullopt;
        }

        StreamAuthData data;
        data.streamKey = streamKey;
        data.clientId = res->getString("client_id");
        data.isAuthorized = res->getBoolean("is_active");

        data.authToken = authToken;

        return data;
    }
    catch (sql::SQLException& e)
    {
        LOG_ERROR("[AuthRepo] SQL执行异常: " + std::string(e.what()) + " [Code: " + std::to_string(e.getErrorCode()) + "]");
        return std::nullopt;
    }
}

bool HybridAuthRepository::validateAuthData(const StreamAuthData& data, std::string_view expectedStreamKey,
                                            std::string_view expectedClientId, std::string_view expectedToken)
{
    //强制校验 clientId，防止数据错位或投毒
    if (data.streamKey != expectedStreamKey ||
        data.clientId != expectedClientId ||
        data.authToken != expectedToken)
    {
        LOG_ERROR("[HybridAuthRepository] Data validation failed for stream: " + std::string(expectedStreamKey) +
            " | client: " + std::string(expectedClientId));
        return false;
    }

    return !data.streamKey.empty() && !data.clientId.empty() && !data.authToken.empty();
}

std::string HybridAuthRepository::buildCacheKey(const std::string& streamKey, const std::string& clientId)
{
    std::string key;
    key.reserve(10 + streamKey.size() + 1 + clientId.size());
    key += "auth_data:";
    key += streamKey;
    key += ':';
    key += clientId;
    return key;
}

std::optional<StreamAuthData> HybridAuthRepository::tryGetFromCache(const std::string& cacheKey) const
{
    try
    {
        return _cacheManager.getAuthDataFromCacheByKey(cacheKey);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("[HybridAuthRepository] Cache access error: " +std::string(e.what()));
        return std::nullopt;
    }
    catch (...)
    {
        LOG_ERROR("[HybridAuthRepository] Cache unknown error");
        return std::nullopt;
    }
}

void HybridAuthRepository::cacheAuthData(const std::string& cacheKey, const StreamAuthData& data) const
{
    try
    {
        _cacheManager.setAuthDataToCacheByKey(cacheKey, data, _cacheTTL);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("[HybridAuthRepository] Cache set failed: " + std::string(e.what()));
    }
}

void HybridAuthRepository::cacheNegativeResult(const std::string& cacheKey, int ttl) const
{
    try
    {
        _cacheManager.setEmptyAuthDataToCache(cacheKey, ttl);
    }
    catch (...)
    {
    }
}

HybridAuthRepository::Stats HybridAuthRepository::getStats() const
{
    Stats s{};
    s.cache_hits = _cacheHits.load();
    s.cache_misses = _cacheMisses.load();
    s.db_hits = _dbHits.load();
    s.db_misses = _dbMisses.load();
    s.db_errors = _dbErrors.load();
    s.validation_failures = _validationFailures.load();

    const uint64_t total = s.cache_hits + s.cache_misses;
    s.cache_hit_rate = (total > 0) ? (static_cast<double>(s.cache_hits) / static_cast<double>(total)) : 0.0;
    return s;
}

void HybridAuthRepository::resetStats()
{
    _cacheHits = 0;
    _cacheMisses = 0;
    _dbHits = 0;
    _dbMisses = 0;
    _dbErrors = 0;
    _validationFailures = 0;
}

bool HybridAuthRepository::isHealthy()
{
    return _cacheManager.ping() && _dbManager.isConnected();
}
