//
// Created by wxx on 12/15/25.
//

#ifndef STREAMGATE_ISTREAMSTATEMANAGER_H
#define STREAMGATE_ISTREAMSTATEMANAGER_H
#include "StreamTask.h"
#include <vector>
#include <optional>
#include <chrono>

/**
 * @brief 任务唯一标识符
 */
struct TaskIdentifier
{
    std::string streamName;
    std::string clientId;
    StreamType type;
};

/**
 * @brief 流状态管理器接口 (IStreamStateManager)
 * 职责：维护推流 (Publisher) 和播放 (Player) 的实时生命周期。
 */
class IStreamStateManager
{
public:
    virtual ~IStreamStateManager() = default;

    //任务生命周期
    /**
     * @brief   注册或更新一个流任务
     *          对于推流任务，如果已存在则更新
     *          对于拉流任务，新增或刷新活跃时间
     * @param task  要注册/更新的任务
     * @return  true 成功， false 底层存储故障
     */
    virtual bool registerTask(const StreamTask& task) =0;

    /**
     * @brief   移除指定客户端的流任务
     * @param stream_name 移除指定客户端的流任务
     * @param client_id 客户端 ID
     * @return  true 成功移除（或任务本就不存在），false 底层故障
     */
    virtual bool deregisterTask(const std::string& stream_name,
                                const std::string& client_id) =0;

    /**
     * @brief 联动清理流下的所有成员（推流者和所有播放者）
     * @param stream_name 流名称
     */
    virtual void deregisterAllMembers(const std::string& stream_name) = 0;

    /**
     * @brief 获取特定流名下的所有客户端 ID (核心索引查询)
     * @param stream_name
     * @return 客户端 ID 列表，若流不存在则返回空列表
     */
    [[nodiscard]] virtual std::vector<std::string> getStreamClientIds(const std::string& stream_name) const =0;

    /**
    *@brief   更新任务活跃时间 (心跳)
    * @param stream_name
    * @param client_id
    */
    [[nodiscard]] virtual bool touchTask(const std::string& stream_name,
                                         const std::string& client_id) const =0;

    //数据查询与监控
    [[nodiscard]] virtual std::optional<StreamTask> getTask(const std::string& stream_name,
                                                            const std::string& client_id) const =0;

    /**
   * @brief 获取所有活跃的推流任务（用于监控面板或全局统计）
   * @return  所有推流任务列表
   */
    [[nodiscard]] virtual std::vector<StreamTask> getAllPublisherTasks() const =0;

    /**
    * @brief 获取当前在线统计
    */
    [[nodiscard]] virtual size_t getActivePublisherCount() const =0;
    [[nodiscard]] virtual size_t getActivePlayerCount() const =0;

    /**
     * @brief 获取特定流的当前发布者详情
     * @param stream_name stream_name 流名
     * @return 存在则返回 Task，不存在返回 std::nullopt
     */
    [[nodiscard]] virtual std::optional<StreamTask> getPublisherTask(const std::string& stream_name) const =0;

    /**
     * @brief 后端存储健康检查
     */
    [[nodiscard]] virtual bool isHealthy() const =0;

    /**
    * @brief   扫描超时的任务（用于定时器清理僵尸流）
    * @param timeout
    * @return  超时的任务列表（调用方负责 deregister）
    */
    [[nodiscard]] virtual std::vector<StreamTask> scanTimeoutTasks(std::chrono::milliseconds timeout) =0;

    //批量操作优化 (默认实现)
    virtual size_t registerTasksBatch(const std::vector<StreamTask>& tasks)
    {
        size_t successCount = 0;
        for (const auto& t : tasks)
        {
            if (registerTask(t))
                successCount++;
        }
        return successCount;
    }

    [[nodiscard]] virtual size_t touchTasksBatch(const std::vector<TaskIdentifier>& tasks) const
    {
        size_t successCount = 0;
        for (const auto& [streamName, clientId,type] : tasks)
        {
            if (touchTask(streamName, clientId))
                successCount++;
        }
        return successCount;
    }

    virtual size_t deregisterTasksBatch(const std::vector<TaskIdentifier>& tasks)
    {
        size_t successCount = 0;
        for (const auto& [streamName, clientId,type] : tasks)
        {
            if (deregisterTask(streamName, clientId))
                successCount++;
        }
        return successCount;
    }
};

#endif //STREAMGATE_ISTREAMSTATEMANAGER_H
