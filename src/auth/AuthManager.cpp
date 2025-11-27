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
    constexpr int JSON_PARSE_FAIL = 300;
    constexpr int CACHE_AUTH_FAIL = 100;
    constexpr int DB_AUTH_FAIL = 200;
    constexpr int DB_ERROR = 201;
    constexpr int CACHE_ERROR = 101;
}

AuthManager::AuthManager() = default;


AuthManager& AuthManager::instance()
{
    static AuthManager instance;
    return instance;
}

bool AuthManager::parseBody(const std::string& body, std::string& streamName, std::string& clientId)
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
        if (obj.contains("stream_name") && obj.at("stream_name").is_string())
        {
            streamName = boost::json::value_to<std::string>(obj.at("stream_name"));
        }
        else
        {
            std::cerr << "AuthManager: Missing or invalid 'stream_name' in JSON." << std::endl;
            return false;
        }

        if (obj.contains("client_id") && obj.at("client_id").is_string())
        {
            clientId = boost::json::value_to<std::string>(obj.at("client_id"));
        }
        else
        {
            std::cerr << "AuthManager: Missing or invalid 'client_id' in JSON." << std::endl;
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

int AuthManager::performCheck(const std::string& streamName, const std::string& clientId)
{
    std::future<int> cacheGetFuture = CacheManager::instance().getAuthResult(streamName, clientId);

    int cacheResult = AuthError::CACHE_ERROR;
    try
    {
        if (cacheGetFuture.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready)
        {
            cacheResult = cacheGetFuture.get();
        }
        else
        {
            std::cerr << "AuthManager: Cache lookup timeout for " << streamName << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "AuthManager: Cache future exception: " << e.what() << std::endl;
    }
    if (cacheResult == CACHE_HIT_SUCCESS)
    {
        std::cout << "AuthManager: Cache HIT (Success) for " << streamName << std::endl;
        return AuthError::SUCCESS;
    }

    if (cacheResult == CACHE_HIT_FAILURE)
    {
        std::cout << "AuthManager: Cache HIT (Failure) for " << streamName << std::endl;
        return AuthError::CACHE_AUTH_FAIL;
    }

    std::future<int> dbFuture = DBManager::instance().asyncCheckStream(streamName, clientId);

    int dbResult = AuthError::DB_ERROR;
    try
    {
        dbResult = dbFuture.get();
    }
    catch (const std::exception& e)
    {
        std::cerr << "AuthManager: DB future exception: " << e.what() << std::endl;
    }

    int finalAuthResult;

    if (dbResult == 0)
    {
        finalAuthResult = AuthError::SUCCESS;
        CacheManager::instance().setAuthResult(streamName, clientId, AuthError::SUCCESS);
    }
    else if (dbResult > 0)
    {
        finalAuthResult = AuthError::DB_AUTH_FAIL;
        CacheManager::instance().setAuthResult(streamName, clientId, AuthError::DB_AUTH_FAIL);
    }
    else
    {
        finalAuthResult = AuthError::DB_ERROR;
    }

    return finalAuthResult;
}

int AuthManager::checkHook(const std::string& body)
{
    std::string streamName, clientId;

    if (! parseBody(body, streamName, clientId))
    {
        return AuthError::JSON_PARSE_FAIL;
    }

    return performCheck(streamName, clientId);
}