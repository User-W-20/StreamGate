//
// Created by X on 2025/11/16.
//
#include "AuthManager.h"
#include "DBManager.h"
#include "CacheManager.h"
#include "Logger.h"
#include <iostream>
#include <stdexcept>
#include <thread>


namespace AuthError
{
    constexpr int SUCCESS = 0;
    constexpr int CACHE_AUTH_FAIL = 100;
    constexpr int DB_AUTH_FAIL = 200;
    constexpr int DB_ERROR = 201;
    constexpr int RUNTIME_ERROR = 500;
}

AuthManager::AuthManager() = default;


AuthManager& AuthManager::instance()
{
    static AuthManager instance;
    return instance;
}

bool AuthManager::parseBody(const std::string& body,
                            std::string& streamName,
                            std::string& clientId,
                            std::string& authToken)
{
    if (body.empty())
    {
        LOG_ERROR("AuthManager: Request body is empty.");
        return false;
    }

    try
    {
        //解析JSON
        boost::json::value jv = boost::json::parse(body);
        boost::json::object const& obj = jv.as_object();

        //提取字段
        if (obj.contains("streamKey") && obj.at("streamKey").is_string())
        {
            streamName = boost::json::value_to<std::string>(obj.at("streamKey"));
        }
        else
        {
            LOG_ERROR("AuthManager: Missing or invalid 'streamKey' in JSON.");
            return false;
        }

        if (obj.contains("clientId") && obj.at("clientId").is_string())
        {
            clientId = boost::json::value_to<std::string>(obj.at("clientId"));
        }
        else
        {
            LOG_ERROR("AuthManager: Missing or invalid 'clientId' in JSON.");
            return false;
        }

        if (obj.contains("authToken") && obj.at("authToken").is_string())
        {
            authToken = boost::json::value_to<std::string>(obj.at("authToken"));
        }
        else
        {
            LOG_ERROR("AuthManager: Missing or invalid 'authToken' in JSON.");
            return false;
        }

        return true;
    }
    catch (const boost::json::system_error& e)
    {
        LOG_ERROR("AuthManager: JSON parse error: " +std::string(e.what()));
        return false;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("AuthManager: Unexpected error during JSON parsing: " +std::string(e.what()));
        return false;
    }
}

int AuthManager::performCheck(const std::string& streamName, const std::string& clientId, const std::string& authToken)
{
    int final_result_code = AuthError::DB_AUTH_FAIL;

    try
    {
        std::future<int> cache_future = CacheManager::instance().getAuthResult(streamName, clientId);
        int cache_result = cache_future.get();

        if (cache_result == CACHE_HIT_SUCCESS)
        {
            LOG_INFO("AuthManager: Cache HIT (SUCCESS) for stream: "+streamName);
            return AuthError::SUCCESS;
        }

        if (cache_result == CACHE_HIT_FAILURE)
        {
            LOG_INFO("AuthManager: Cache HIT (FAILURE) for stream: "+streamName);
            return AuthError::CACHE_AUTH_FAIL;
        }

        if (cache_result == CACHE_MISS)
        {
            LOG_INFO("AuthManager: Cache MISS. Falling back to DB.");
        }
        else
        {
            LOG_WARN("AuthManager Warning: Cache service error. Falling back to DB.");
        }

        std::future<int> db_future = DBManager::instance().asyncCheckStream(streamName, clientId, authToken);
        int db_result = db_future.get();

        if (db_result == 1)
        {
            final_result_code = AuthError::SUCCESS;
        }
        else if (db_result == 0)
        {
            final_result_code = AuthError::DB_AUTH_FAIL;
        }
        else if (db_result == -1 || db_result == -2)
        {
            LOG_ERROR("AuthManager Error: DB internal error (Code: "+std::to_string(db_result)+"). Fail Closed.");
            return AuthError::DB_ERROR;
        }

        int cache_set_value = (final_result_code == AuthError::SUCCESS) ? CACHE_HIT_SUCCESS : CACHE_HIT_FAILURE;
        CacheManager::instance().setAuthResult(streamName, clientId, cache_set_value);

        return final_result_code;
    }
    catch (const std::future_error& e)
    {
        LOG_FATAL("AuthManager FATAL Error: Future/Thread Pool exception: "+std::string(e.what())+ ". Fail Closed.");
        return AuthError::RUNTIME_ERROR;
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("AuthManager FATAL Error: Unhandled exception: " +std::string(e.what())+". Fail Closed.");
        return AuthError::RUNTIME_ERROR;
    }
}

int AuthManager::checkHook(const std::string& body)
{
    LOG_DEBUG("checkHook body: "+body);
    std::string streamName, clientId, authToken;

    if (! parseBody(body, streamName, clientId, authToken))
    {
        return 400;
    }

    int auth_error_code = performCheck(streamName, clientId, authToken);

    if (auth_error_code == AuthError::SUCCESS)
    {
        return 200;
    }
    if (auth_error_code == AuthError::CACHE_AUTH_FAIL ||
        auth_error_code == AuthError::DB_AUTH_FAIL)
    {
        return 403;
    }

    return 403;
}

bool AuthManager::authenticate(const std::string& streamKey, const std::string& clientId, const std::string& authToken)
{
    bool isAuthenticated = false;

    try
    {
        std::string cacheKey = streamKey + ":" + clientId + ":" + authToken;
        std::future<int> cache_future = CacheManager::instance().getAuthResult(cacheKey);

        int cache_result = cache_future.get();

        if (cache_result == CACHE_HIT_SUCCESS)
        {
            LOG_INFO("AuthManager: Cache HIT (SUCCESS) for stream: " +streamKey);
            return true;
        }

        if (cache_result == CACHE_HIT_FAILURE)
        {
            LOG_INFO("AuthManager: Cache HIT (FAILURE) for stream: "+streamKey);
            return false;
        }

        if (cache_result == CACHE_MISS)
        {
            LOG_INFO("AuthManager: Cache MISS for stream: "+streamKey+". Falling back to DB.");
        }
        else
        {
            LOG_WARN("AuthManager Warning: Cache service error for stream: "+streamKey+". Falling back to DB.");
        }

        std::future<int> db_future = DBManager::instance().asyncCheckStream(streamKey, clientId, authToken);

        int db_result = db_future.get();

        if (db_result == 1)
        {
            isAuthenticated = true;
        }
        else if (db_result == 0)
        {
            isAuthenticated = false;
        }
        else
        {
            LOG_ERROR(
                "AuthManager Error: DB check failed internally (Code: "+std::to_string(db_result)+") for stream " +
                streamKey+ ". Fail Closed.");
            return false;
        }

        CacheManager::instance().setAuthResult(cacheKey, isAuthenticated ? CACHE_HIT_SUCCESS : CACHE_HIT_FAILURE);

        return isAuthenticated;
    }
    catch (const std::future_error& e)
    {
        LOG_FATAL(
            "AuthManager FATAL Error: Future/Thread Pool exception for "+streamKey+": "+std::string(e.what())+
            ". Fail Closed.");
        return false;
    }
    catch (const std::exception& e)
    {
        LOG_FATAL(
            "AuthManager FATAL Error: Unhandled exception during authentication: "+std::string(e.what())+
            ". Fail Closed.");
        return false;
    }
}