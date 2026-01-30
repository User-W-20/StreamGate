//
// Created by X on 2025/11/16.
//
#include "AuthManager.h"
#include "ThreadPool.h"
#include "Logger.h"
#include <stdexcept>

AuthManager::AuthManager(std::unique_ptr<IAuthRepository> repo, ThreadPool& pool, Config config)
    : _repository(std::move(repo)), _pool(pool), _config(config)
{
    if (!_repository)
    {
        throw std::invalid_argument("AuthManager: 注入的 Repository 为空");
    }
}

AuthManager::~AuthManager()
{
    _shutdown.store(true);
    LOG_INFO("AuthManager: Shutdown complete.");
}

int AuthManager::performAuthLogic(const std::string& sk, const std::string& cid, const std::string& tk) const
{
    try
    {
        auto authData = _repository->getAuthData(sk, cid, tk);

        if (authData.has_value() && authData->isAuthorized)
        {
            return AuthError::SUCCESS;
        }

        LOG_INFO("AuthManager: 授权拒绝 (StreamKey: " + sk + ")");
        return AuthError::AUTH_DENIED;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("AuthManager: Repository 异常: " + std::string(e.what()));
        return AuthError::RUNTIME_ERROR;
    }
    catch (...)
    {
        LOG_ERROR("AuthManager: Repository 发生未知异常");
        return AuthError::RUNTIME_ERROR;
    }
}

bool AuthManager::checkAuth(const std::string& streamKey, const std::string& clientId, const std::string& token) const
{
    if (_shutdown.load())return false;

    // 使用 shared_ptr 管理 promise 生命周期，防止同步侧超时退出后导致的内存非法访问
    const auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();

    // 提交任务到线程池
    _pool.submit([this,streamKey,clientId,token,promise]()
    {
        if (_shutdown.load())return;

        const int result = this->performAuthLogic(streamKey, clientId, token);

        try
        {
            // [安全补丁]: 捕获可能的 std::future_error
            // 当调用方因超时已经销毁了 future 或放弃等待时，这里会抛出异常
            promise->set_value(result);
        }
        catch (...)
        {
            // 忽略异常：说明调用方已超时退出
        }
    });

    // 等待结果，带超时控制
    auto status = future.wait_for(_config.timeout);

    if (status == std::future_status::timeout)
    {
        LOG_WARN("AuthManager: 鉴权超时 (StreamKey: " + streamKey+ ")");
        return false; // 超时视作鉴权失败
    }

    try
    {
        return future.get() == AuthError::SUCCESS;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("AuthManager: 获取 future 结果异常: " + std::string(e.what()));
        return false;
    }
}

void AuthManager::checkAuthAsync(const std::string& streamKey, const std::string& clientId, const std::string& token,
                                 AuthCallback cb) const
{
    if (_shutdown.load() || !cb)return;

    // 异步模式：直接投递任务，不阻塞当前线程
    _pool.submit([this,streamKey,clientId,token,cb=std::move(cb)]()
    {
        if (_shutdown.load())return;

        const int result = this->performAuthLogic(streamKey, clientId, token);

        // 执行回调
        try
        {
            cb(result);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("AuthManager: 异步回调执行异常: " + std::string(e.what()));
        }
    });
}

void AuthManager::checkAuthAsync(const AuthRequest& req, AuthCallback cb) const
{
    if (_shutdown.load() || !cb)return;

    // 直接透传 req 成员给线程池
    _pool.submit([this,req,cb=std::move(cb)]()
    {
        if (_shutdown.load())return;

        const int result = this->performAuthLogic(req.streamKey, req.clientId, req.authToken);

        try
        {
            cb(result);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("AuthManager: 异常: " + std::string(e.what()));
        }
    });
}
