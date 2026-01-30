//
// Created by X on 2025/11/16.
//

#ifndef STREAMGATE_AUTHMANAGER_H
#define STREAMGATE_AUTHMANAGER_H
#include <string>
#include <future>
#include <functional>
#include "IAuthRepository.h"
#include "ThreadPool.h"

/**
 * @brief 鉴权管理器 (AuthManager)
 * * [线程安全说明]:
 * 1. checkAuthAsync 的回调函数 (Callback) 会在【线程池的工作线程】中执行。
 * 调用者需确保回调内的操作是线程安全的，或者将任务重新分发 (dispatch) 到目标线程。
 * * [并发语义说明]:
 * 1. checkAuth (同步带超时) 在超时发生时会立即返回 false。
 * 2. 此时后台的鉴权任务【不会被真正杀死】，它会继续在线程池运行直至结束。
 * 3. 通过内部的 _shutdown 标志位和 shared_ptr<promise>，确保在管理器析构后，
 * 残留任务不会访问非法内存，也不会导致 std::future_error 崩溃。
 */
class AuthManager
{
public:
    struct Config
    {
        std::chrono::milliseconds timeout{5000}; // 默认 5 秒超时
    };

    // 错误码定义
    enum AuthError
    {
        SUCCESS = 0,
        AUTH_DENIED = 1,
        RUNTIME_ERROR = -1
    };

    AuthManager(std::unique_ptr<IAuthRepository> repo, ThreadPool& pool, Config config);
    ~AuthManager();

    // 禁用拷贝
    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;

    //核心业务接口
    /**
     * @brief 同步鉴权接口（带超时保护）
     * @param streamKey
     * @param clientId
     * @param token
     * @return true 鉴权通过; false 鉴权失败或超时
     */
    [[nodiscard]] bool checkAuth(const std::string& streamKey, const std::string& clientId,
                                 const std::string& token) const;

    /**
     * @brief 异步鉴权接口（回调模式）
     * @param cb 回调函数，接收 AuthError 错误码
     */
    using AuthCallback = std::function<void(int)>;
    void checkAuthAsync(const std::string& streamKey, const std::string& clientId, const std::string& token,
                        AuthCallback cb) const;
    void checkAuthAsync(const AuthRequest& req, AuthCallback cb) const;

    // 状态查询
    [[nodiscard]] bool isShutdown() const
    {
        return _shutdown.load();
    }

private:
    /**
     * @brief 内部核心逻辑：唯一的数据查询出口
     */
    [[nodiscard]] int performAuthLogic(const std::string& sk, const std::string& cid, const std::string& tk) const;

    std::unique_ptr<IAuthRepository> _repository;
    ThreadPool& _pool;
    Config _config;
    std::atomic<bool> _shutdown{false};
};
#endif //STREAMGATE_AUTHMANAGER_H
