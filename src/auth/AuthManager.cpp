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
    catch (const boost::system::system_error& e)
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

void AuthManager::performCheckAsync(const HookParams& params,
                                    AuthCallback callback)
{
    ThreadPool& shared_pool = DBManager::instance().getThreadPool();

    auto start_auth_check = [this,params,callback]()
    {
        const std::string& streamName = params.streamKey;
        const std::string& clientId = params.clientId;
        const std::string& authToken = params.authToken;

        try
        {
            std::future<int> cache_future = CacheManager::instance().getAuthResult(streamName, clientId);
            int cache_result = cache_future.get();

            if (cache_result == CACHE_HIT_SUCCESS)
            {
                LOG_INFO("AuthManager: Cache HIT (SUCCESS) for stream: "+streamName);
                callback(AuthError::SUCCESS);
                return;
            }

            if (cache_result == CACHE_HIT_FAILURE)
            {
                LOG_INFO("AuthManager: Cache HIT (FAILURE) for stream: "+streamName);
                callback(AuthError::CACHE_AUTH_FAIL);
                return;
            }

            if (cache_result == CACHE_MISS)
            {
                LOG_INFO("AuthManager: Cache MISS. Falling back to DB for stream: " + streamName);
            }
            else
            {
                LOG_WARN("AuthManager Warning: Cache service error. Falling back to DB.");
            }

            std::future<int> db_future = DBManager::instance().asyncCheckStream(streamName, clientId, authToken);
            int db_result = db_future.get();

            int final_result_code = AuthError::DB_AUTH_FAIL;;

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
                callback(AuthError::DB_ERROR);
                return;
            }

            int cache_set_value = (final_result_code == AuthError::SUCCESS) ? CACHE_HIT_SUCCESS : CACHE_HIT_FAILURE;
            CacheManager::instance().setAuthResult(streamName, clientId, cache_set_value);

            callback(final_result_code);
        }
        catch (const std::exception& e)
        {
            LOG_FATAL("AuthManager FATAL Error in Shared Worker thread: "+std::string(e.what())+". Fail Closed.");
            callback(AuthError::RUNTIME_ERROR);
        }
    };

    shared_pool.submit(start_auth_check);
}