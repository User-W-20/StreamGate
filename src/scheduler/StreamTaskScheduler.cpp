//
// Created by wxx on 2025/12/19.
//
#include "StreamTaskScheduler.h"
#include "Logger.h"
#include "EnumToString.h"
#include <thread>
#include <chrono>
#include <set>
#include <utility>

StreamTaskScheduler::StreamTaskScheduler(AuthManager& authMgr, IStreamStateManager& stateMgr, const NodeConfig& nodeCfg,
                                         Config cfg)
    : _authManager(authMgr), _stateManager(stateMgr), _node_config(nodeCfg), _config(cfg)
{
}

StreamTaskScheduler::~StreamTaskScheduler()
{
    stop();
}

void StreamTaskScheduler::start()
{
    if (_running.exchange(true))return;
    _cleanup_thread = std::thread(&StreamTaskScheduler::timeoutCleanupThread, this);
    LOG_INFO("Scheduler: 清理线程已启动");
}

void StreamTaskScheduler::stop()
{
    if (!_running.exchange(false))return;
    {
        std::lock_guard<std::mutex> lock(_cleanup_mutex);
        _cleanup_cv.notify_all();
    }

    if (_cleanup_thread.joinable())_cleanup_thread.join();
    LOG_INFO("Scheduler: 已停止");
}

//推流逻辑
void StreamTaskScheduler::onPublish(const std::string& stream_name, const std::string& client_id,
                                    const std::string& auth_token, StreamProtocol protocol, SchedulerCallback callback)
{
    _totalPublishReq.fetch_add(1, std::memory_order_relaxed);
    if (!validateRequest(stream_name, client_id, auth_token, callback))return;

    AuthRequest authReq{stream_name, client_id, auth_token};
    _authManager.checkAuthAsync(
        authReq, [this,stream_name,client_id,auth_token,protocol,callback=std::move(callback)](int code)
        {
            try
            {
                if (code != 0) // SUCCESS = 0
                {
                    _authFail.fetch_add(1, std::memory_order_relaxed);
                    if (callback)
                        callback({SchedulerResult::Error::AUTH_FAILED, std::nullopt, "鉴权拒绝"});
                    return;
                }

                auto [ip,port] = selectBestNode(protocol);
                auto task = createTask(stream_name, client_id, auth_token, StreamType::PUBLISHER, protocol, ip, port);

                LOG_INFO("DEBUG: About to register task - stream: " + stream_name +
                    ", client: " + client_id +
                    ", task_id: " + std::to_string(task.task_id));

                if (auto existing = _stateManager.getPublisherTask(stream_name))
                {
                    LOG_WARN("DEBUG: Publisher already exists! stream: " + stream_name +
                        ", existing client: " + existing->client_id +
                        ", existing task_id: " + std::to_string(existing->task_id));
                }

                // 原子化注册：由 StateManager 处理 "已存在" 冲突
                if (!_stateManager.registerTask(task))
                {
                    LOG_ERROR("DEBUG: registerTask() returned false! stream: " + stream_name);

                    if (callback)
                        callback({SchedulerResult::Error::ALREADY_PUBLISHING, std::nullopt, "该流已在推送中"});
                    return;
                }

                _successPub.fetch_add(1, std::memory_order_relaxed);
                if (callback)
                    callback({SchedulerResult::Error::SUCCESS, task, "推流授权成功"});
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("onPublish callback exception: " + std::string(e.what()));
                if (callback)
                    callback({SchedulerResult::Error::INTERNAL_ERROR, std::nullopt, "Internal error"});
            }
            catch (...)
            {
                LOG_ERROR("onPublish callback unknown exception");
                if (callback)
                    callback({SchedulerResult::Error::INTERNAL_ERROR, std::nullopt, "Unknown error"});
            }
        });
}

void StreamTaskScheduler::onPublishDone(const std::string& stream_name, const std::string& client_id) const
{
    LOG_INFO("Scheduler: 收到推流结束回调 [Stream: " + stream_name + ", Client: " + client_id + "]");

    auto task_opt = _stateManager.getTask(stream_name, client_id);

    if (task_opt && task_opt->type == StreamType::PUBLISHER)
    {
        LOG_INFO("Scheduler: 身份确认，执行联动清理...");
        _stateManager.deregisterAllMembers(stream_name);
    }
    else
    {
        LOG_WARN("Scheduler: 忽略无效回调。该 Client 不再是该流的活跃发布者。");
    }
}

//播放逻辑
void StreamTaskScheduler::onPlay(const std::string& stream_name, const std::string& client_id,
                                 const std::string& auth_token, StreamProtocol protocol, SchedulerCallback callback)
{
    _totalPlayReq.fetch_add(1, std::memory_order_relaxed);
    if (!validateRequest(stream_name, client_id, auth_token, callback))return;

    AuthRequest authReq{stream_name, client_id, auth_token};
    _authManager.checkAuthAsync(
        authReq, [this,stream_name,client_id,auth_token,protocol,callback=std::move(callback)](int code)
        {
            try
            {
                if (code != 0)
                {
                    _authFail.fetch_add(1, std::memory_order_relaxed);
                    if (callback)
                        callback({SchedulerResult::Error::AUTH_FAILED, std::nullopt, "鉴权拒绝"});
                    return;
                }

                //反查推流端是否存在
                auto pub = _stateManager.getPublisherTask(stream_name);
                if (!pub)
                {
                    if (callback)
                        callback({SchedulerResult::Error::NO_PUBLISHER, std::nullopt, "找不到活跃推流端"});
                    return;
                }

                // 强行绑定到推流端所在的边缘节点 IP/Port
                auto task = createTask(stream_name, client_id, auth_token, StreamType::PLAYER, protocol, pub->server_ip,
                                       pub->server_port);

                if (!_stateManager.registerTask(task))
                {
                    if (callback)
                        callback({SchedulerResult::Error::STATE_STORE_ERROR, std::nullopt, "状态注册失败"});
                    return;
                }

                _successPlay.fetch_add(1, std::memory_order_relaxed);
                if (callback)
                    callback({SchedulerResult::Error::SUCCESS, task, "播放授权成功"});
            }
            catch (const std::exception& e)
            {
                LOG_ERROR("onPlay callback exception: " + std::string(e.what()));
                if (callback)
                    callback({SchedulerResult::Error::INTERNAL_ERROR, std::nullopt, "Internal error"});
            }
            catch (...)
            {
                LOG_ERROR("onPlay callback unknown exception");
                if (callback)
                    callback({SchedulerResult::Error::INTERNAL_ERROR, std::nullopt, "Unknown error"});
            }
        });
}

void StreamTaskScheduler::onPlayDone(const std::string& stream_name, const std::string& client_id) const
{
    // 播放端注销是单点操作
    _stateManager.deregisterTask(stream_name, client_id);
}

//辅助方法
std::pair<std::string, int> StreamTaskScheduler::selectBestNode(StreamProtocol protocol)
{
    const std::vector<NodeEndpoint>* nodes = nullptr;

    // 根据协议路由到不同的集群（RTMP/SRT vs HTTP/HLS）
    if (protocol == StreamProtocol::RTMP || protocol == StreamProtocol::SRT)
    {
        nodes = &_node_config.rtmp_srt;
    }
    else
    {
        nodes = &_node_config.http_hls;
    }

    if (!nodes || nodes->empty())return {"127.0.0.1", 1935};

    // 简单的 Round-Robin 负载均衡
    const auto& node = (*nodes)[_nodeIndex.fetch_add(1, std::memory_order_relaxed) % nodes->size()];
    return {node.host, node.port};
}

//辅助方法
void StreamTaskScheduler::timeoutCleanupThread()
{
    while (_running.load())
    {
        try
        {
            //调用对齐后的毫秒级扫描接口
            auto expired = _stateManager.scanTimeoutTasks(_config.task_timeout);

            if (!expired.empty())
            {
                std::vector<TaskIdentifier> targets;
                std::set<std::string> publisher_died_streams; //记录哪些流的主播挂了

                for (const auto& t : expired)
                {
                    targets.push_back({t.stream_name, t.client_id, t.type});
                    _tasksCleaned.fetch_add(1, std::memory_order_relaxed);

                    if (t.type == StreamType::PUBLISHER)
                    {
                        publisher_died_streams.insert(t.stream_name);
                    }
                }

                //高性能批量注销
                _stateManager.deregisterTasksBatch(targets);

                //联动清理：如果主播超时，清理该流所有成员
                for (const auto& stream : publisher_died_streams)
                {
                    LOG_WARN("Scheduler: 主播超时 [" + stream + "]. 执行全员清场...");
                    _stateManager.deregisterAllMembers(stream);
                }
                LOG_INFO("Scheduler: 自动回收了 " + std::to_string(targets.size()) + " 条超时任务");
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Scheduler: 清理任务循环异常: " + std::string(e.what()));
        }

        // 使用 condition_variable 优雅休眠，支持即时唤醒停止
        std::unique_lock<std::mutex> lock(_cleanup_mutex);
        if (_cleanup_cv.wait_for(lock, _config.cleanup_interval, [this] { return !_running.load(); }))
        {
            break;
        }
    }
}

bool StreamTaskScheduler::validateRequest(const std::string& stream_name, const std::string& client_id,
                                          const std::string& auth_token, const SchedulerCallback& callback)
{
    if (stream_name.empty() || client_id.empty() || auth_token.empty())
    {
        if (callback)
            callback({SchedulerResult::Error::INTERNAL_ERROR, std::nullopt, "参数缺失"});

        return false;
    }

    return true;
}

StreamTask StreamTaskScheduler::createTask(const std::string& stream_name, const std::string& client_id,
                                           const std::string& auth_token, StreamType type, StreamProtocol protocol,
                                           const std::string& ip, int port)
{
    StreamTask task;
    task.task_id = _nextTaskId.fetch_add(1, std::memory_order_relaxed);
    task.stream_name = stream_name;
    task.client_id = client_id;
    task.auth_token = auth_token;
    task.type = type;
    task.protocol = protocol;
    task.server_ip = ip;
    task.server_port = port;
    task.start_time = std::chrono::system_clock::now();
    task.last_active_time = task.start_time;
    return task;
}

/**
 * @brief 获取调度器运行状态度量数据
 * @note 采用 relaxed 内存序读取原子计数器，保证高性能且无锁
 * @return 包含请求数、成功率、鉴权失败数及清理统计的任务度量结构体
 */
StreamTaskScheduler::Metrics StreamTaskScheduler::getMetrics() const
{
    Metrics m{};

    // 使用 load() 配合 memory_order_relaxed 保证高性能读取
    m.total_publish_req = _totalPublishReq.load(std::memory_order_relaxed);
    m.total_play_req = _totalPlayReq.load(std::memory_order_relaxed);
    m.success_pub = _successPub.load(std::memory_order_relaxed);
    m.success_play = _successPlay.load(std::memory_order_relaxed);
    m.auth_failures = _authFail.load(std::memory_order_relaxed);
    m.tasks_cleaned = _tasksCleaned.load(std::memory_order_relaxed);

    return m;
}
