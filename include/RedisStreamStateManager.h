//
// Created by wxx on 12/16/25.
//

#ifndef STREAMGATE_REDISSTREAMSTATEMANAGER_H
#define STREAMGATE_REDISSTREAMSTATEMANAGER_H
#include "CacheManager.h"
#include "IStreamStateManager.h"
#include <string>
#include <vector>
#include <optional>
#include <chrono>

class CacheManager;
struct StreamTask;
enum class StreamType;

/**
 * @brief  基于 Redis 的直播流任务状态管理器（支持 Publisher 与 Player）
 *
 * 负责管理：
*  - 任务注册 / 注销
 * - 心跳更新（touch）
 * - 超时扫描回收
 * - 索引维护：active_pubs 集合、players 集合、stream/player_count 计数器、全局在线人数
 *
 *  线程安全：假设由外部保证同步，或依赖 Redis 操作的原子性
 */
class RedisStreamStateManager : public IStreamStateManager
{
public:
    static constexpr int TASK_TTL_SEC = 60;

    /**
     * @brief 构造函数
     * @param cacheMgr  已连接的 CacheManager 实例（Redis 客户端封装）
     */
    explicit RedisStreamStateManager(CacheManager& cacheMgr);

    [[nodiscard]] std::vector<std::string> getStreamClientIds(const std::string& stream_name) const override;

    //核心任务生命周期管理

    /**
     *@brief  注册一个新任务（Publisher 或 Player）
     * @param task
     * @return  成功返回 true；失败时已完整回滚所有副作用，返回 false
     */
    bool registerTask(const StreamTask& task) override;

    /**
     * @brief 按 stream_name + client_id 注销指定任务
     * @param stream_name
     * @param client_id
     * @return  若任务存在并成功清理，返回 true
     */
    bool deregisterTask(const std::string& stream_name, const std::string& client_id) override;

    void deregisterAllMembers(const std::string& stream_name) override;

    //查询接口

    [[nodiscard]] std::optional<StreamTask> getTask(const std::string& stream_name,
                                                    const std::string& client_id) const override;

    [[nodiscard]] std::optional<StreamTask> getPublisherTask(const std::string& stream_name) const override;

    [[nodiscard]] std::vector<StreamTask> getPlayerTasks(const std::string& stream_name) const;

    [[nodiscard]] std::vector<StreamTask> getAllPublisherTasks() const override;

    //生命周期维护

    /**
     *@brief  刷新任务的 last_active_time 并延长 TTL
     * @param stream_name
     * @param client_id
     * @note  客户端需定期调用此接口发送 heartbeat
     */
    [[nodiscard]] bool touchTask(const std::string& stream_name, const std::string& client_id) const override;

    /**
     *@brief 扫描并回收超时任务，同时清理其所有索引
     * @param timeout
     * @return 返回已回收的任务列表（可用于日志或通知）
     */
    std::vector<StreamTask> scanTimeoutTasks(std::chrono::milliseconds timeout) override;

    //统计信息

    [[nodiscard]] size_t getActivePublisherCount() const override; // 当前活跃 Publisher 数量

    [[nodiscard]] size_t getActivePlayerCount() const override; // 全局在线 Player 总数

    [[nodiscard]] size_t getPlayerCount(const std::string& stream_name) const; // 某 stream 的在线观众数

    //健康检查

    [[nodiscard]] bool isHealthy() const override; // 检查底层 Redis 连接是否正常
private:
    //成员变量
    CacheManager& _cacheManager; // 底层 Redis 客户端引用

    //索引注册/注销逻辑
    bool registerPublisherIndices(const StreamTask& task) const; // 注册 Publisher 相关索引
    bool registerPlayerIndices(const StreamTask& task) const; // 注册 Player 相关索引

    void deregisterPublisherIndices(const std::string& stream_name) const; // 清理 Publisher 索引
    void deregisterPlayerIndices(const std::string& stream_name, const std::string& client_id) const; // 清理 Player 索引

    size_t deregisterTasksBatch(const std::vector<TaskIdentifier>& tasks) override;

    //内部辅助函数
    [[nodiscard]] std::optional<StreamTask> getTaskByKey(const std::string& task_key) const; // 通过完整 key 加载任务

    //Redis Key 构造器（统一命名规范）
    [[nodiscard]] static std::string buildTaskKey(const std::string& stream_name, const std::string& client_id);
    //task:stream:client
    [[nodiscard]] static std::string buildPublisherKey(const std::string& stream_name); //pub:stream
    [[nodiscard]] static std::string buildPlayerSetKey(const std::string& stream_name); //players:stream
    [[nodiscard]] static std::string buildStreamMetaKey(const std::string& stream_name); //meta:stream
    [[nodiscard]] static std::string buildActivePublishersKey(); //active_pubs (global set)
    [[nodiscard]] static std::string buildGlobalPlayerCountKey(); //global_players(hash with "total")
    [[nodiscard]] static std::string buildTaskTimestampZSetKey(); //task_timestamp(zset for time out)
};
#endif //STREAMGATE_REDISSTREAMSTATEMANAGER_H
