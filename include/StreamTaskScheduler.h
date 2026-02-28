//
// Created by wxx on 12/18/25.
//

#ifndef STREAMGATE_STREAMTASKSCHEDULER_H
#define STREAMGATE_STREAMTASKSCHEDULER_H
#include "IStreamStateManager.h"
#include "AuthManager.h"
#include "StreamTask.h"
#include "NodeConfig.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>
#include <functional>
#include <optional>

class StreamTaskScheduler
{
public:
    struct Config
    {
        std::chrono::seconds cleanup_interval{30};
        std::chrono::seconds task_timeout{60};
    };

    /**
     * @brief 统一的异步结果结构
     */
    struct SchedulerResult
    {
        enum class Error
        {
            SUCCESS,
            ALREADY_PUBLISHING,
            NO_PUBLISHER,
            AUTH_FAILED,
            STATE_STORE_ERROR,
            INTERNAL_ERROR
        } error = Error::SUCCESS;

        std::optional<StreamTask> task;
        std::string message;

        [[nodiscard]] bool isSuccess() const
        {
            return error == Error::SUCCESS;
        }
    };

    struct Metrics
    {
        uint64_t total_publish_req;
        uint64_t success_pub;
        uint64_t total_play_req;
        uint64_t success_play;
        uint64_t auth_failures;
        uint64_t tasks_cleaned;
        uint64_t last_update_ms;
    };

    // 统一回调类型
    using SchedulerCallback = std::function<void (const SchedulerResult&)>;

    /**
     *@brief 构造函数，注入认证和状态管理依赖
     * @param authMgr
     * @param stateMgr
     * @param nodeCfg
     * @param cfg
     */
    StreamTaskScheduler(AuthManager& authMgr, IStreamStateManager& stateMgr, const NodeConfig& nodeCfg,
                        Config cfg);
    ~StreamTaskScheduler();

    // 禁用拷贝
    StreamTaskScheduler(const StreamTaskScheduler&) = delete;
    StreamTaskScheduler& operator=(const StreamTaskScheduler&) = delete;

    // --- 核心业务接口 ---
    /**
     * @brief 处理推流请求 (on_publish hook)
     * @param stream_name 流名称
     * @param client_id 客户端 ID
     * @param auth_token 认证 Token
     * @param protocol 推流协议
     * @param callback 异步回调
     */
    void onPublish(const std::string& stream_name, const std::string& client_id,
                   const std::string& auth_token,
                   StreamProtocol protocol, SchedulerCallback callback);

    /**
     *@brief  处理推流结束 (on_publish_done hook)
     * @param stream_name
     * @param client_id
     */
    void onPublishDone(const std::string& stream_name, const std::string& client_id) const;

    /**
     * @brief 处理拉流请求 (on_play hook)
     * @param stream_name
     * @param client_id
     * @param auth_token
     * @param protocol
     * @param callback
     */
    void onPlay(const std::string& stream_name, const std::string& client_id,
                const std::string& auth_token,
                StreamProtocol protocol, SchedulerCallback callback);

    /**
     *@brief 处理拉流结束 (on_play_done hook)
     * @param stream_name
     * @param client_id
     */
    void onPlayDone(const std::string& stream_name, const std::string& client_id) const;

    // --- 生命周期管理 ---
    /**
     * @brief 启动调度器（启动超时清理定时器等）
     */
    void start();

    /**
     * @brief 停止调度器
     */
    void stop();

    Metrics getMetrics() const;

private:
    static bool validateRequest(const std::string& stream_name, const std::string& client_id,
                                const std::string& auth_token,
                                const SchedulerCallback& callback);
    // 内部：选择最优节点
    [[nodiscard]] std::pair<std::string, int> selectBestNode(StreamProtocol protocol);
    StreamTask createTask(const std::string& stream_name, const std::string& client_id, const std::string& auth_token,
                          StreamType type, StreamProtocol protocol, const std::string& ip, int port);
    void timeoutCleanupThread();

    // 依赖项
    AuthManager& _authManager;
    IStreamStateManager& _stateManager;
    NodeConfig _node_config;
    Config _config;

    // 运行状态
    std::atomic<bool> _running{false};
    std::thread _cleanup_thread;
    std::mutex _cleanup_mutex;
    std::condition_variable _cleanup_cv;

    // 轮询调度
    std::atomic<size_t> _nodeIndex{0};
    std::atomic<uint64_t> _nextTaskId{1000};

    // 统计指标
    mutable std::atomic<uint64_t> _totalPublishReq{0};
    mutable std::atomic<uint64_t> _successPub{0};
    mutable std::atomic<uint64_t> _totalPlayReq{0};
    mutable std::atomic<uint64_t> _successPlay{0};
    mutable std::atomic<uint64_t> _authFail{0};
    mutable std::atomic<uint64_t> _tasksCleaned{0};
};
#endif //STREAMGATE_STREAMTASKSCHEDULER_H
