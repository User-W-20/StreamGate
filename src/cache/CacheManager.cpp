#include "CacheManager.h"

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include "ConfigLoader.h"

namespace net = boost::asio;
using namespace std::chrono;

CacheManager::CacheManager()
{
    _client = std::make_unique<cpp_redis::client>();
}

CacheManager::~CacheManager()
{
    if (_client)
    {
        _client->disconnect(true);
    }
}

CacheManager& CacheManager::instance()
{
    static CacheManager instance;
    return instance;
}

std::string CacheManager::buildKey(const std::string& streamKey, const std::string& clientId)
{
    return "auth:" + streamKey + ":" + clientId;
}

void CacheManager::connect()
{
    std::cout << "CacheManager: Initializing cpp_redis client (self-managed I/O)..." << std::endl;

    try
    {
        const std::string host = ConfigLoader::instance().getString("REDIS_HOST");
        const int port = ConfigLoader::instance().getInt("REDIS_PORT");
        _cacheTTL = ConfigLoader::instance().getInt("CACHE_TTL_SECONDS", 300);

        _client->connect(host,
                         port,
                         [](const std::string& host, size_t port, cpp_redis::client::connect_state status)
                         {
                             if (status == cpp_redis::client::connect_state::ok)
                             {
                                 std::cout << "CacheManager: Successfully connected to Redis at " << host << ":" << port
                                     << std::endl;
                             }
                             else
                             {
                                 std::cerr << "CacheManager: Failed to connect to Redis at " << host << ":" << port <<
                                     std::endl;
                             }
                         });
    }
    catch (const std::exception& e)
    {
        std::cerr << "CacheManager FATAL ERROR: " << e.what() << std::endl;
        throw;
    }
}

int CacheManager::performSyncGet(const std::string& key)
{
    std::promise<int> result_promise;
    std::future<int> result_future = result_promise.get_future();

    _client->get(key,
                 [&result_promise](const cpp_redis::reply& reply)
                 {
                     try
                     {
                         if (reply.is_error())
                         {
                             std::cerr << "Redis GET Error: " << reply.as_string() << std::endl;
                             result_promise.set_value(CACHE_ERROR);
                         }
                         else if (reply.is_null())
                         {
                             result_promise.set_value(CACHE_MISS);
                         }
                         else
                         {
                             int result = boost::lexical_cast<int>(reply.as_string());
                             result_promise.set_value(result == 0 ? CACHE_HIT_SUCCESS : CACHE_HIT_FAILURE);
                         }
                     }
                     catch (const std::exception& e)
                     {
                         std::cerr << "Redis GET Cast Error: " << e.what() << std::endl;
                         result_promise.set_value(CACHE_ERROR);
                     }
                 });

    _client->commit();

    if (result_future.wait_for(seconds(1)) == std::future_status::timeout)
    {
        std::cerr << "Redis GET Timeout for key: " << key << std::endl;
        return CACHE_ERROR;
    }

    return result_future.get();
}

void CacheManager::performSyncSet(const std::string& key, int result)
{
    _client->setex(key,
                   _cacheTTL,
                   std::to_string(result),
                   [](const cpp_redis::reply& reply)
                   {
                       if (reply.is_error())
                       {
                           std::cerr << "Redis SETEX Error: " << reply.as_string() << std::endl;
                       }
                   });
    _client->commit();
}

std::future<int> CacheManager::getAuthResult(const std::string& streamKey, const std::string& clientId)
{
    std::string key = buildKey(streamKey, clientId);

    return DBManager::instance().getThreadPool().submit(
        &CacheManager::performSyncGet,
        this,
        key);
}

std::future<void> CacheManager::setAuthResult(const std::string& streamKey, const std::string& clientId, int result)
{
    std::string key = buildKey(streamKey, clientId);

    return DBManager::instance().getThreadPool().submit(
        &CacheManager::performSyncSet,
        this,
        key,
        result);
}