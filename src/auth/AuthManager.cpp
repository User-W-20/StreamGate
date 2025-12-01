//
// Created by X on 2025/11/16.
//
#include "AuthManager.h"
#include "DBManager.h"
#include "CacheManager.h"
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
        std::cerr << "AuthManager: Request body is empty." << std::endl;
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
            std::cerr << "AuthManager: Missing or invalid 'streamKey' in JSON." << std::endl;
            return false;
        }

        if (obj.contains("clientId") && obj.at("clientId").is_string())
        {
            clientId = boost::json::value_to<std::string>(obj.at("clientId"));
        }
        else
        {
            std::cerr << "AuthManager: Missing or invalid 'clientId' in JSON." << std::endl;
            return false;
        }

        if (obj.contains("authToken") && obj.at("authToken").is_string())
        {
            authToken = boost::json::value_to<std::string>(obj.at("authToken"));
        }
        else
        {
            std::cerr << "AuthManager: Missing or invalid 'authToken' in JSON." << std::endl;
            return false;
        }

        return true;
    }
    catch (const boost::json::system_error& e)
    {
        std::cerr << "AuthManager: JSON parse error: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e)
    {
        std::cerr << "AuthManager: Unexpected error during JSON parsing: " << e.what() << std::endl;
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
            std::cout << "AuthManager: Cache HIT (SUCCESS) for stream: " << streamName << std::endl;
            return AuthError::SUCCESS;
        }

        if (cache_result == CACHE_HIT_FAILURE)
        {
            std::cout << "AuthManager: Cache HIT (FAILURE) for stream: " << streamName << std::endl;
            return AuthError::CACHE_AUTH_FAIL;
        }

        if (cache_result == CACHE_MISS)
        {
            std::cout << "AuthManager: Cache MISS. Falling back to DB." << std::endl;
        }
        else
        {
            std::cerr << "AuthManager Warning: Cache service error. Falling back to DB." << std::endl;
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
            std::cerr << "AuthManager Error: DB internal error (Code: " << db_result << "). Fail Closed." << std::endl;
            return AuthError::DB_ERROR;
        }

        int cache_set_value = (final_result_code == AuthError::SUCCESS) ? CACHE_HIT_SUCCESS : CACHE_HIT_FAILURE;
        CacheManager::instance().setAuthResult(streamName, clientId, cache_set_value);

        return final_result_code;
    }
    catch (const std::future_error& e)
    {
        std::cerr << "AuthManager FATAL Error: Future/Thread Pool exception: " << e.what() << ". Fail Closed." <<
            std::endl;
        return AuthError::RUNTIME_ERROR;
    }
    catch (const std::exception& e)
    {
        std::cerr << "AuthManager FATAL Error: Unhandled exception: " << e.what() << ". Fail Closed." << std::endl;
        return AuthError::RUNTIME_ERROR;
    }
}

int AuthManager::checkHook(const std::string& body)
{
    std::cout << "checkHook body: " << body << std::endl;
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
            std::cout << "AuthManager: Cache HIT (SUCCESS) for stream: " << streamKey << std::endl;
            return true;
        }

        if (cache_result == CACHE_HIT_FAILURE)
        {
            std::cout << "AuthManager: Cache HIT (FAILURE) for stream: " << streamKey << std::endl;
            return false;
        }

        if (cache_result == CACHE_MISS)
        {
            std::cout << "AuthManager: Cache MISS for stream: " << streamKey << ". Falling back to DB." << std::endl;
        }
        else
        {
            std::cerr << "AuthManager Warning: Cache service error for stream: " << streamKey << ". Falling back to DB."
                << std::endl;
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
            std::cerr << "AuthManager Error: DB check failed internally (Code: " << db_result << ") for stream " <<
                streamKey << ". Fail Closed." << std::endl;
            return false;
        }

        CacheManager::instance().setAuthResult(cacheKey, isAuthenticated ? CACHE_HIT_SUCCESS : CACHE_HIT_FAILURE);

        return isAuthenticated;
    }
    catch (const std::future_error& e)
    {
        std::cerr << "AuthManager FATAL Error: Future/Thread Pool exception for "
            << streamKey << ": " << e.what() << ". Fail Closed." << std::endl;
        return false;
    }
    catch (const std::exception& e)
    {
        std::cerr << "AuthManager FATAL Error: Unhandled exception during authentication: "
            << e.what() << ". Fail Closed." << std::endl;
        return false;
    }
}