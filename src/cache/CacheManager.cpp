#include "CacheManager.h"

#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <functional>
#include <boost/lexical_cast.hpp>
#include "ConfigLoader.h"
#include "Logger.h"

namespace net = boost::asio;
using namespace std::chrono;

CacheManager& CacheManager::instance()
{
    static CacheManager instance;
    return instance;
}

CacheManager::CacheManager()
{
    _client = std::make_unique<cpp_redis::client>();
}

CacheManager::~CacheManager()
{
    try
    {
        if (_client)
            _client->disconnect();

        _io_context.stop();
        _work_guard.reset();

        for (auto& t : _io_threads)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
    }
    catch (...)
    {
    }
}

void CacheManager::start_io_loop()
{
    ConfigLoader& config = ConfigLoader::instance();

    const size_t thread_count = config.getInt("REDIS_IO_THREADS", 2);

    _work_guard = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
        boost::asio::make_work_guard(_io_context));

    for (size_t i = 0; i < thread_count; ++i)
    {
        _io_threads.emplace_back([this]()
        {
            try
            {
                _io_context.run();
            }
            catch (const std::exception& e)
            {
                LOG_FATAL("CacheManager I/O thread crashed: "+std::string(e.what()));
            }
        });
    }

    LOG_INFO("CacheManager: Initializing cpp_redis client (self-managed I/O)...");

    try
    {
        const std::string host = ConfigLoader::instance().getString("REDIS_HOST");
        const int port = ConfigLoader::instance().getInt("REDIS_PORT");
        _cacheTTL = config.getInt("CACHE_TTL_SECONDS", 300);

        _client->connect(host,
                         port,
                         [this](const std::string& host, size_t port, cpp_redis::client::connect_state status)
                         {
                             if (status == cpp_redis::client::connect_state::dropped)
                             {
                                 LOG_WARN("Redis connection dropped. Reconnecting...");

                                 try
                                 {
                                     _client->connect(host, port, nullptr);
                                 }
                                 catch (...)
                                 {
                                 }
                             }
                         });

        _client->sync_commit(std::chrono::milliseconds(50));
        LOG_INFO(
            "CacheManager: Redis connection initiated. I/O loop running on " +std::to_string(thread_count)+" threads.");
    }
    catch (const std::exception& e)
    {
        LOG_WARN(
            "CacheManager Warning: Configuration or initial setup failed (Proceeding without Redis): " +std::string(e.
                what()));
    }
}

std::string CacheManager::buildKey(const std::string& streamKey, const std::string& clientId)
{
    return "auth:" + streamKey + ":" + clientId;
}

int CacheManager::performSyncGet(const std::string& key)
{
    try
    {
        if (! _client->is_connected())
        {
            LOG_ERROR("Redis GET Error: not connected");
            return CACHE_ERROR;
        }

        auto future_reply = _client->get(key);

        _client->sync_commit(milliseconds(50));

        auto reply = future_reply.get();

        if (reply.is_null())
            return CACHE_MISS;

        if (! reply.is_string())
            return CACHE_ERROR;

        std::string v = reply.as_string();

        if (v == "1")
            return CACHE_HIT_SUCCESS;
        if (v == "0")
            return CACHE_HIT_FAILURE;

        return CACHE_MISS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Redis GET Exception: "+std::string(e.what()));
        return CACHE_ERROR;
    }
    catch (...)
    {
        return CACHE_ERROR;
    }
}


void CacheManager::performSyncSet(const std::string& key, int result)
{
    try
    {
        if (! _client->is_connected())
        {
            LOG_ERROR("Redis SET Error: client disconnected");
            return;
        }

        std::string value = (result == CACHE_HIT_SUCCESS) ? "1" : "0";

        _client->setex(key,
                       _cacheTTL,
                       value,
                       [](const cpp_redis::reply& reply)
                       {
                           if (reply.is_error())
                           {
                               LOG_ERROR("Redis SETEX Error: " +reply.as_string());
                           }
                       });

        _client->sync_commit(std::chrono::milliseconds(200));
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Redis I/O Error during SET for key "+key+": " +std::string(e.what()));
    }
    catch (...)
    {
        LOG_ERROR("CacheManager Unknown Error during SET for key "+key+".");
    }
}

std::future<int> CacheManager::getAuthResult(const std::string& streamKey, const std::string& clientId)
{
    if (! is_ready())
    {
        LOG_WARN("CacheManager Warning: Redis service is DOWN or not ready. Reporting CACHE_MISS.");

        std::promise<int> p;
        p.set_value(CACHE_MISS);
        return p.get_future();
    }

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

std::future<int> CacheManager::getAuthResult(const std::string& cacheKey)
{
    return DBManager::instance().getThreadPool().submit(
        &CacheManager::performSyncGet,
        this,
        cacheKey);
}

void CacheManager::setAuthResult(const std::string& cacheKey, int result)
{
    DBManager::instance().getThreadPool().submit(
        &CacheManager::performSyncSet,
        this,
        cacheKey,
        result);
}

bool CacheManager::is_ready() const
{
    if (! _io_threads_running || ! _is_initialized)
    {
        return false;
    }

    if (! _client)
    {
        return false;
    }

    return _client->is_connected();
}


void CacheManager::force_disconnect()
{
    if (! _client)
        return;

    LOG_WARN("CacheManager: Forcing synchronous disconnect and stopping I/O context.");

    if (_client && _client->is_connected())
    {
        _client->disconnect(true);
    }

    if (_work_guard)
    {
        _work_guard.reset();
    }

    _io_context.stop();

    for (auto& t : _io_threads)
    {
        if (t.joinable())
        {
            LOG_WARN("CacheManager: Joining I/O thread...");
            t.join();
        }
    }

    _io_threads.clear();

    LOG_INFO("CacheManager: Disconnect completed. I/O threads stopped.");
}

void CacheManager::reconnect()
{
    if (! _client)
        return;

    LOG_INFO("CacheManager: Attempting to forcibly restart I/O loop and reconnect client.");

    instance().start_io_loop();

    try
    {
        const std::string host = ConfigLoader::instance().getString("REDIS_HOST");
        const int port = ConfigLoader::instance().getInt("REDIS_PORT");

        _client->connect(host,
                         port,
                         [this](const std::string& h, size_t p, cpp_redis::client::connect_state status)
                         {
                             if (status == cpp_redis::client::connect_state::dropped)
                             {
                                 LOG_WARN("Redis connection dropped. Reconnecting...");
                                 try
                                 {
                                     _client->connect(h, p, nullptr);
                                 }
                                 catch (...)
                                 {
                                 }
                             }
                         });

        _client->sync_commit(std::chrono::milliseconds(500));

        LOG_INFO("CacheManager: Reconnection successful.");
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("CacheManager: Reconnection failed: " + std::string(e.what()));
    }
}

std::optional<StreamAuthData> CacheManager::getAuthDataFromCache(const std::string& streamKey)
{
    if (! is_ready())
    {
        LOG_WARN("CacheManager Warning: Redis service is DOWN or not ready. Reporting CACHE_MISS.");
        return std::nullopt;
    }

    std::string key = buildKey(streamKey, "");

    auto serializedData = performSyncGetString(key);

    if (serializedData.has_value())
    {
        return StreamAuthData::deserialize(serializedData.value());
    }

    return std::nullopt;
}

void CacheManager::setAuthDataToCache(const StreamAuthData& data, int ttl)
{
    if (! is_ready())
    {
        LOG_WARN("CacheManager Warning: Redis service is DOWN or not ready. Skipping cache write.");
        return;
    }

    std::string serializedData = data.serialize();

    if (serializedData.empty())
    {
        LOG_ERROR("CacheManager: Failed to serialize StreamAuthData.");
        return;
    }

    std::string key = buildKey(data.streamKey, "");

    performSyncSetString(key, serializedData, ttl);
}

std::optional<std::string> CacheManager::performSyncGetString(const std::string& key)
{
    std::lock_guard<std::mutex> lock(_client_mutex);

    if (! _client || _client->is_connected())
    {
        LOG_WARN("CacheManager: Redis client is not connected when attempting sync GET for key: " + key);
        return std::nullopt;
    }

    try
    {
        std::optional<std::string> result = std::nullopt;
        bool commit_success = false;

        _client->get(key,
                     [&result,&commit_success,&key](cpp_redis::reply& reply)
                     {
                         if (reply.is_error())
                         {
                             LOG_ERROR("Redis GET Error for key " +key+ ": "+reply.as_string());
                             commit_success = false;
                         }
                         else if (reply.is_null())
                         {
                             LOG_INFO("Redis GET: Key not found (NULL reply) for key: " +key);
                             commit_success = true;
                             result = std::nullopt;
                         }
                         else
                         {
                             result = reply.as_string();
                             commit_success = true;
                         }
                     });

        _client->sync_commit(std::chrono::milliseconds(500));

        if (commit_success)
        {
            return result;
        }
        else
        {
            return std::nullopt;
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Redis GET Exception for key "+key+": " +e.what());
        return std::nullopt;
    }
}

void CacheManager::performSyncSetString(const std::string& key, const std::string& value, int ttl)
{
    std::lock_guard<std::mutex> lock(_client_mutex);

    if (! _client || ! _client->is_connected())
    {
        LOG_WARN("CacheManager: Redis client is not connected when attempting sync SET for key: "+key);
        return;
    }

    try
    {
        bool commit_success = false;

        if (ttl > 0)
        {
            _client->setex(key,
                           ttl,
                           value,
                           [&commit_success,&key](cpp_redis::reply& reply)
                           {
                               if (reply.is_error())
                               {
                                   LOG_ERROR("Redis SETEX Error for key " + key + ": " + reply.as_string());
                                   commit_success = false;
                               }
                               else
                               {
                                   commit_success = true;
                               }
                           });
        }

        _client->sync_commit(std::chrono::milliseconds(500));

        if (! commit_success)
        {
            LOG_WARN("Redis SET command failed or did not receive success reply for key: "+key);
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("Redis SET Exception (General) for key "+key+": " + e.what());
    }
}