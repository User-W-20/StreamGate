#include "CacheManager.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <future>
#include "ConfigLoader.h"
#include "HookServer.h"
#include "Logger.h"

using json = nlohmann::json;

//单例实现
CacheManager& CacheManager::instance()
{
    static CacheManager inst;
    return inst;
}

CacheManager::~CacheManager()
{
    _redis.reset();
}

//初始化（线程安全）
void CacheManager::init(const std::string& host, int port, int pool_size, const std::string& password)
{
    bool expected = false;
    if (!_io_running.compare_exchange_strong(expected, true))
    {
        LOG_WARN("CacheManager: Already initialized, skipping.");
        return;
    }

    try
    {
        sw::redis::ConnectionOptions opts;
        opts.host = host;
        opts.port = port;
        if (!password.empty())
        {
            opts.password = password;
        }

        sw::redis::ConnectionPoolOptions pool_opts;
        pool_opts.size = pool_size;

        auto redis_instance = std::make_unique<sw::redis::Redis>(opts, pool_opts);

        if (redis_instance->ping() != "PONG")
        {
            throw std::runtime_error("Redis server is not responding to PING");
        }

        _redis = std::move(redis_instance);

        _io_running.store(true, std::memory_order_relaxed);

        LOG_INFO("CacheManager: Initialized with pool size " + std::to_string(pool_size));
    }
    catch (const std::exception& e)
    {
        _io_running.store(false, std::memory_order_relaxed);
        LOG_ERROR("CacheManager: Initialization failed: " + std::string(e.what()));
        throw;
    }
}

//辅助
std::string CacheManager::buildKey(const std::string& streamKey, const std::string& clientId)
{
    return "auth:" + streamKey + ":" + clientId;
}

std::optional<std::string> CacheManager::getString(const std::string& key) const
{
    if (!_redis)return std::nullopt;

    try
    {
        auto val = _redis->get(key);
        return val ? std::make_optional(*val) : std::nullopt;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] Redis GET failed for key '"+key+"': "+e.what());
        return std::nullopt;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("[CacheManager ERROR] Unexpected exception in getString('" +key+"'): "+e.what());
        return std::nullopt;
    }
}

void CacheManager::setString(const std::string& key, const std::string& value, int ttl) const
{
    if (!_redis) return;

    //Safety:never allow permanent keys
    if (ttl <= 0)
    {
        ttl = _cacheTTL;
    }

    try
    {
        _redis->setex(key, ttl, value);
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] Redis SETEX failed for key '" +key+"': " +e.what());
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("[CacheManager ERROR] Unexpected exception in setString('" +key+"'): "+e.what());
    }
}

//Auth Result
int CacheManager::getAuthResult(const std::string& streamKey, const std::string& clientId) const
{
    return getAuthResultByKey(buildKey(streamKey, clientId));
}

void CacheManager::setAuthResult(const std::string& streamKey, const std::string& clientId, int result) const
{
    setAuthResultByKey(buildKey(streamKey, clientId), result);
}

int CacheManager::getAuthResultByKey(const std::string& cacheKey) const
{
    if (!_io_running.load(std::memory_order_acquire))
    {
        LOG_ERROR("CacheManager: Attempted to get auth result before init. Key: " + cacheKey);
        return CACHE_ERROR;
    }

    try
    {
        if (auto val = getString(cacheKey))
        {
            try
            {
                return std::stoi(*val);
            }
            catch (...)
            {
                LOG_ERROR("CacheManager: Malformed cache data for key: " + cacheKey);
                return CACHE_ERROR;
            }
        }
        return CACHE_MISS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("CacheManager: Unexpected error in getAuthResultByKey: " + std::string(e.what()));
        return CACHE_ERROR;
    }
}

void CacheManager::setAuthResultByKey(const std::string& cacheKey, int result) const
{
    if (!_io_running.load(std::memory_order_acquire))
    {
        LOG_ERROR("CacheManager: Attempted to set auth result before init.");
        return;
    }

    setString(cacheKey, std::to_string(result), _cacheTTL);
}

//Auth Data
std::optional<StreamAuthData> CacheManager::getAuthDataFromCache(const std::string& streamKey) const
{
    return getAuthDataFromCacheByKey(buildKey(streamKey, "data"));
}

std::optional<StreamAuthData> CacheManager::getAuthDataFromCacheByKey(const std::string& customKey) const
{
    auto opt = getString(customKey);
    if (!opt || *opt == "__EMPTY__")
    {
        return std::nullopt;
    }

    try
    {
        auto j = json::parse(*opt);
        return j.get<StreamAuthData>();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("[CacheManager ERROR] Failed to parse StreamAuthData from key "+customKey+"': "+e.what());
        return std::nullopt;
    }
}

void CacheManager::setAuthDataToCache(const StreamAuthData& data, int ttl) const
{
    setAuthDataToCacheByKey(buildKey(data.streamKey, "data"), data, ttl);
}

void CacheManager::setAuthDataToCacheByKey(const std::string& key, const StreamAuthData& data, int ttl) const
{
    if (ttl <= 0) ttl = _cacheTTL;
    try
    {
        json j = data; //Use to_json
        setString(key, j.dump(), ttl);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("[CacheManager ERROR] Failed to serialize StreamAuthData to key '" +key+"': " +e.what());
    }
}

void CacheManager::setEmptyAuthDataToCache(const std::string& key, int ttl) const
{
    if (ttl <= 0) ttl = _cacheTTL;
    setString(key, "__EMPTY__", ttl);
}

//Hash
bool CacheManager::hashSet(const std::string& key, const std::unordered_map<std::string, std::string>& fields) const
{
    if (!_redis)return false;

    try
    {
        _redis->hmset(key, fields.begin(), fields.end());
        return true;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] HMSET failed for key '" +key+"': " +e.what());
        return false;
    }
}

std::unordered_map<std::string, std::string> CacheManager::hashGetAll(const std::string& key) const
{
    if (!_redis) return {};

    try
    {
        std::unordered_map<std::string, std::string> result;
        _redis->hgetall(key, std::inserter(result, result.end()));
        return result;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] HGETALL failed for key '"+key+"': "+e.what());
        return {};
    }
}

bool CacheManager::hashDel(const std::string& key, const std::string& field) const
{
    if (!_redis) return false;

    try
    {
        return _redis->hdel(key, field) > 0;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] HDEL failed for key '" +key+ "', field '" +field+"': " +e.what());
        return false;
    }
}

bool CacheManager::hashKeyDel(const std::string& key) const
{
    return keyDel(key);
}

long long CacheManager::hashIncrBy(const std::string& key, const std::string& field, long long increment) const
{
    if (!_redis) return 0;
    try
    {
        return _redis->hincrby(key, field, increment);
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] HINCRBY failed for key '"+key+"', field '" +field+"': " +e.what());
        return 0;
    }
}

//Set
bool CacheManager::setAdd(const std::string& key, const std::string& member) const
{
    if (!_redis) return false;
    try
    {
        _redis->sadd(key, member);
        return true;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] SADD failed for key '"+key+"': " +e.what());
        return false;
    }
}

bool CacheManager::setAdd(const std::string& key, const std::vector<std::string>& members) const
{
    if (!_redis) return false;

    try
    {
        _redis->sadd(key, members.begin(), members.end());
        return true;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] SADD (vector) failed for key '"+key+ "': " +e.what());
        return false;
    }
}

bool CacheManager::setRem(const std::string& key, const std::string& member) const
{
    if (!_redis) return false;

    try
    {
        return _redis->srem(key, member) > 0;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] SREM failed for key '" +key+ "': "+e.what());
        return false;
    }
}

std::vector<std::string> CacheManager::setMembers(const std::string& key) const
{
    if (!_redis) return {};
    try
    {
        std::vector<std::string> members;
        _redis->smembers(key, std::back_inserter(members));
        return members;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] SMEMBERS failed for key '"+key+"': "+e.what());
        return {};
    }
}

size_t CacheManager::setCard(const std::string& key) const
{
    if (!_redis) return 0;

    try
    {
        return _redis->scard(key);
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] SCARD failed for key '" +key+"': "+e.what());
        return 0;
    }
}

bool CacheManager::setDel(const std::string& key) const
{
    return keyDel(key);
}

//ZSet
bool CacheManager::zsetAdd(const std::string& key, double score, const std::string& member) const
{
    if (!_redis) return false;

    try
    {
        _redis->zadd(key, member,score);
        return true;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] ZADD failed for key '" +key+"': " +e.what());
        return false;
    }
}

std::vector<std::string> CacheManager::zsetRangeByScore(const std::string& key, double min, double max) const
{
    if (!_redis) return {};

    try
    {
        std::vector<std::string> members;
        sw::redis::BoundedInterval<double> interval(min, max, sw::redis::BoundType::CLOSED);
        _redis->zrangebyscore(key, interval, std::back_inserter(members));
        return members;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] ZRANGEBYSCORE failed for key '" +key+ "': "+e.what());
        return {};
    }
}

bool CacheManager::zsetRem(const std::string& key, const std::string& member) const
{
    if (!_redis) return false;

    try
    {
        return _redis->zrem(key, member) > 0;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] ZREM failed for key '"+key+"': " +e.what());
        return false;
    }
}

//Generic
bool CacheManager::keyExpire(const std::string& key, int seconds) const
{
    if (!_redis) return false;

    if (seconds <= 0)seconds = _cacheTTL;

    try
    {
        return _redis->expire(key, seconds);
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] EXPIRE failed for key '"+key+"': "+e.what());
        return false;
    }
}

bool CacheManager::keyDel(const std::string& key) const
{
    if (!_redis) return false;

    try
    {
        return _redis->del(key) > 0;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] DEL failed for key '"+key+"': "+e.what());
        return false;
    }
}

bool CacheManager::keyExists(const std::string& key) const
{
    if (!_redis) return false;

    try
    {
        return _redis->exists(key) > 0;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("[CacheManager ERROR] EXISTS failed for key '"+key+ "': "+e.what());
        return false;
    }
}

bool CacheManager::ping() const
{
    if (!_io_running.load(std::memory_order_acquire) || !_redis)
    {
        throw std::logic_error("Contract Violation: CacheManager::ping() called before init() or after shutdown()");
    }

    try
    {
        std::string res = _redis->ping();

        return res == "PONG";
    }
    catch (const sw::redis::TimeoutError& e)
    {
        LOG_ERROR("Redis Ping Timeout: " + std::string(e.what()));
        return false;
    }
    catch (const sw::redis::Error& e)
    {
        LOG_ERROR("Redis Connectivity Error: " + std::string(e.what()));
        return false;
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("Unexpected system error during Redis Ping: " + std::string(e.what()));
        return false;
    }
}
