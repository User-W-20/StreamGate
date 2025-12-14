//
// Created by X on 2025/11/16.
//
#include "AuthManager.h"
// #include "DBManager.h"
// #include "CacheManager.h"
#include "ThreadPool.h"
#include "Logger.h"
#include <iostream>
#include <stdexcept>
#include <thread>

AuthManager::AuthManager(std::unique_ptr<IAuthRepository> repository,ThreadPool&sharedPool)
:_repository(std::move(repository)),
_sharedPool(sharedPool)
{
    LOG_INFO("AuthManager initialized with IAuthRepository dependency.");
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
    ThreadPool& shared_pool = _sharedPool;

    auto start_auth_check = [this,params,callback]()
    {
        const std::string& streamName = params.streamKey;
        const std::string& clientId = params.clientId;
        const std::string& authToken = params.authToken;

        try
        {
            LOG_INFO("AuthManager: Submitting request to Repository for stream: " + streamName);

            auto authData=_repository->getAuthData(streamName,clientId,authToken);

            int final_result_code=AuthError::AUTH_DENIED;

           if (authData.has_value()&&authData.value().isAuthorized)
           {
               LOG_INFO("AuthManager: Authorization SUCCESS via Repository for stream: " + streamName);
               final_result_code=AuthError::SUCCESS;
           }else if (authData.has_value()&&!authData.value().isAuthorized)
           {
               LOG_INFO("AuthManager: Data found but Authorization FAILED for stream: " + streamName);
               final_result_code=AuthError::AUTH_DENIED;
           }else
           {
               LOG_WARN("AuthManager: Repository returned nullopt (Not Found or Fault). Authorization Denied.");
               final_result_code=AuthError::AUTH_DENIED;
           }
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