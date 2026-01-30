//
// Created by wxx on 12/17/25.
//
#include "RedisStreamStateManager.h"
#include "StreamTask.h"
#include "Logger.h"
#include <cassert>
#include <chrono>
#include <string_view>
#include <unordered_map>
#include <array>
#include <optional>

// 时间戳合理性范围（2020-01-01 至 2038-01-01 UTC）
static constexpr int64_t MIN_REASONABLE_MS = 1577836800000LL; // 2020-01-01 00:00:00 UTC
static constexpr int64_t MAX_REASONABLE_MS = 2145916800000LL; // 2038-01-01 00:00:00 UTC

// 辅助函数：执行非关键路径操作，失败时仅记录日志而不返回错误（用于清理逻辑）
namespace
{
    void bestEffort(const bool ok, std::string_view op, std::string_view key)
    {
        if (__glibc_likely(ok))
        {
            return;
        }

        std::string msg;
        msg.reserve(op.size() + key.size() + 20);
        msg.append(op).append(" failed for key=").append(key);

        LOG_WARN(msg);
    }
}

// 序列化 / 反序列化
static std::unordered_map<std::string, std::string> serializeTask(const StreamTask& task)
{
    auto to_ms_str = [](const std::chrono::system_clock::time_point tp)
    {
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
    };

    return {
        {"stream_name", task.stream_name},
        {"client_id", task.client_id},
        {"active", "1"},
        {"type", toString(task.type)},
        {"state", toString(task.state)},
        {"protocol", toString(task.protocol)},
        {"server_ip", task.server_ip},
        {"server_port", std::to_string(task.server_port)},
        {"start_time_ms", to_ms_str(task.start_time)},
        {"last_active_time_ms", to_ms_str(task.last_active_time)},
        {"user_id", task.user_id},
        {"auth_token", task.auth_token},
        {"region", task.region.value_or("")},
        {"need_transcode", task.need_transcode ? "1" : "0"},
        {"need_record", task.need_record ? "1" : "0"},
        {"transcoding_profile", task.transcoding_profile}
    };
}

template <typename MapType>
static std::optional<StreamTask> deserializeTask(const MapType& fields)
{
    for (constexpr std::array required{"stream_name", "client_id", "type", "start_time_ms", "last_active_time_ms"};
         const auto& key : required)
    {
        if (!fields.contains(key))
        {
            return std::nullopt;
        }
    }

    StreamTask task;
    task.stream_name = fields.at("stream_name");
    task.client_id = fields.at("client_id");

    const auto& type_str = fields.at("type");
    if (type_str != "publisher" && type_str != "player")
    {
        return std::nullopt;
    }
    task.type = parseType(type_str);

    if (auto it = fields.find("state"); it != fields.end())
    {
        task.state = parseState(it->second);
    }
    else
    {
        task.state = StreamState::INITIALIZING;
    }

    if (auto it = fields.find("protocol"); it != fields.end())
    {
        task.protocol = parseProtocol(it->second);
    }
    else
    {
        task.protocol = StreamProtocol::Unknown;
    }

    if (auto it = fields.find("server_ip"); it != fields.end())
    {
        task.server_ip = it->second;
    }

    task.server_port = 0;

    if (auto it = fields.find("server_port"); it != fields.end())
    {
        try
        {
            task.server_port = std::stoi(it->second);
        }
        catch (...)
        {
        }
    }

    try
    {
        const int64_t start_ms = std::stoll(fields.at("start_time_ms"));
        const int64_t last_ms = std::stoll(fields.at("last_active_time_ms"));

        if (start_ms < MIN_REASONABLE_MS || start_ms > MAX_REASONABLE_MS ||
            last_ms < MIN_REASONABLE_MS || last_ms > MAX_REASONABLE_MS)
        {
            return std::nullopt;
        }

        task.start_time = std::chrono::system_clock::time_point(std::chrono::milliseconds{start_ms});
        task.last_active_time = std::chrono::system_clock::time_point(std::chrono::milliseconds{last_ms});
    }
    catch (...)
    {
        return std::nullopt;
    }

    if (auto it = fields.find("user_id"); it != fields.end())
    {
        task.user_id = it->second;
    }

    if (auto it = fields.find("auth_token"); it != fields.end())
    {
        task.auth_token = it->second;
    }

    if (auto it = fields.find("region"); it != fields.end() && !it->second.empty())
    {
        task.region = it->second;
    }

    task.need_transcode = (fields.contains("need_transcode") && fields.at("need_transcode") == "1");
    task.need_record = (fields.contains("need_record") && fields.at("need_record") == "1");

    if (auto it = fields.find("transcoding_profile"); it != fields.end())
    {
        task.transcoding_profile = it->second;
    }

    return task;
}

// 构造函数
RedisStreamStateManager::RedisStreamStateManager(CacheManager& cacheMgr)
    : _cacheManager(cacheMgr)
{
}

/**
 * @brief 实现接口：获取流下所有成员
 */
std::vector<std::string> RedisStreamStateManager::getStreamClientIds(const std::string& stream_name) const
{
    const std::string member_key = "stream:members:" + stream_name;

    try
    {
        return _cacheManager.setMembers(member_key);
    }
    catch (const sw::redis::Error& err)
    {
        LOG_ERROR("getStreamClientIds failed for key=" + member_key + " error=" + err.what());
        return {};
    }
}

// 核心生命周期管理（幂等注册，修复 Publisher 重连死锁）
bool RedisStreamStateManager::registerTask(const StreamTask& task)
{
    assert(!task.stream_name.empty());
    assert(!task.client_id.empty());

    const std::string task_key = buildTaskKey(task.stream_name, task.client_id);

    if (task.type == StreamType::PUBLISHER)
    {
        auto existing_pub = getPublisherTask(task.stream_name);
        if (existing_pub && existing_pub->client_id != task.client_id)
        {
            LOG_WARN("registerTask: Stream " + task.stream_name +
                " already has a different publisher: " + existing_pub->client_id);
            return false;
        }

        if (existing_pub && existing_pub->client_id == task.client_id)
        {
            LOG_INFO("registerTask: Cleaning old state for reconnecting publisher: " + task.client_id);
            deregisterTask(task.stream_name, task.client_id);
        }
    }
    else
    {
        if (_cacheManager.keyExists(task_key))
        {
            LOG_INFO("registerTask: Cleaning old player state: " + task.client_id);
            deregisterTask(task.stream_name, task.client_id);
        }
    }

    // // 幂等：先清理旧状态（关键修复：允许同 client_id 重连）
    // if (_cacheManager.keyExists(task_key))
    // {
    //     deregisterTask(task.stream_name, task.client_id);
    // }

    if (!_cacheManager.hashSet(task_key, serializeTask(task)))
    {
        LOG_ERROR("registerTask: hashSet failed for task_key=" + task_key);
        return false;
    }

    if (!_cacheManager.keyExpire(task_key, TASK_TTL_SEC))
    {
        LOG_ERROR("registerTask: keyExpire failed, rolling back");
        bestEffort(_cacheManager.keyDel(task_key), "keyDel", task_key);
        return false;
    }

    bool index_ok = false;
    if (task.type == StreamType::PUBLISHER)
    {
        index_ok = registerPublisherIndices(task);
    }
    else
    {
        index_ok = registerPlayerIndices(task);
    }

    if (!index_ok)
    {
        LOG_ERROR("registerTask: index registration failed, rolling back");
        deregisterTask(task.stream_name, task.client_id); //回滚
        return false;
    }

    //保证 ZSet 写入失败也回滚
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    LOG_INFO("DEBUG: now_ms value = " + std::to_string(now_ms));
    LOG_INFO("DEBUG: now_ms as double = " + std::to_string(static_cast<double>(now_ms)));
    LOG_INFO("DEBUG: task_key = " + task_key);
    LOG_INFO("DEBUG: MIN_REASONABLE_MS = " + std::to_string(MIN_REASONABLE_MS));
    LOG_INFO("DEBUG: MAX_REASONABLE_MS = " + std::to_string(MAX_REASONABLE_MS));

    if (now_ms < MIN_REASONABLE_MS || now_ms > MAX_REASONABLE_MS)
    {
        LOG_ERROR("registerTask: Invalid timestamp: " + std::to_string(now_ms));
        deregisterTask(task.stream_name, task.client_id);
        return false;
    }

    LOG_INFO("DEBUG: About to call zsetAdd...");

    if (!_cacheManager.zsetAdd(buildTaskTimestampZSetKey(), static_cast<double>(now_ms), task_key))
    {
        LOG_ERROR("registerTask: zsetAdd failed, rolling back");
        deregisterTask(task.stream_name, task.client_id);
        return false;
    }

    LOG_INFO("registerTask: Successfully registered - stream=" + task.stream_name +
        ", client=" + task.client_id + ", type=" + toString(task.type));

    return true;
}

bool RedisStreamStateManager::deregisterTask(const std::string& stream_name, const std::string& client_id)
{
    const auto task_opt = getTask(stream_name, client_id);
    if (!task_opt)
    {
        const std::string task_key = buildTaskKey(stream_name, client_id);
        (void)_cacheManager.zsetRem(buildTaskTimestampZSetKey(), task_key);
        return true;
    }

    std::vector<TaskIdentifier> targets = {
        {stream_name, client_id, task_opt->type}
    };

    return deregisterTasksBatch(targets) > 0;
}

// 查询接口
std::optional<StreamTask> RedisStreamStateManager::getTask(const std::string& stream_name,
                                                           const std::string& client_id) const
{
    return getTaskByKey(buildTaskKey(stream_name, client_id));
}

std::optional<StreamTask> RedisStreamStateManager::getPublisherTask(const std::string& stream_name) const
{
    const std::string pub_key = buildPublisherKey(stream_name);

    const auto fields = _cacheManager.hashGetAll(pub_key);
    if (fields.empty() || !fields.contains("active") || fields.at("active") != "1")
    {
        return std::nullopt;
    }

    return deserializeTask(fields);
}

std::vector<StreamTask> RedisStreamStateManager::getPlayerTasks(const std::string& stream_name) const
{
    std::vector<StreamTask> tasks;
    const std::string player_set_key = buildPlayerSetKey(stream_name);
    for (const auto client_ids = _cacheManager.setMembers(player_set_key); const auto& id : client_ids)
    {
        if (auto task = getTask(stream_name, id))
        {
            tasks.push_back(*task);
        }
    }
    return tasks;
}

std::vector<StreamTask> RedisStreamStateManager::getAllPublisherTasks() const
{
    std::vector<StreamTask> tasks;
    const std::string active_pubs_key = buildActivePublishersKey();
    for (const auto stream_names = _cacheManager.setMembers(active_pubs_key); const auto& name : stream_names)
    {
        if (auto task = getPublisherTask(name))
        {
            tasks.push_back(*task);
        }
    }

    return tasks;
}

// 生命周期维护（毫秒级 touch）
bool RedisStreamStateManager::touchTask(const std::string& stream_name, const std::string& client_id) const
{
    const std::string task_key = buildTaskKey(stream_name, client_id);
    const std::string zset_key = buildTaskTimestampZSetKey();
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    try
    {
        auto pipe = _cacheManager.createPipeline();

        pipe.hset(task_key, "last_active_time_ms", std::to_string(now_ms))
            .expire(task_key, std::chrono::seconds(TASK_TTL_SEC))
            .zadd(zset_key, task_key, static_cast<double>(now_ms));

        if (auto results = pipe.exec(); !results.get<bool>(1))
        {
            if (!_cacheManager.keyDel(task_key))
            {
                LOG_WARN("Cleanup of orphaned task hash failed: " + task_key);
            }
            return false;
        }

        return true;
    }
    catch (const sw::redis::Error& err)
    {
        LOG_ERROR("Touch task failed: " + std::string(err.what()));
        return false;
    }
}

//分布式超时扫描（实现 Double-Check 乐观锁，防止误杀）
std::vector<StreamTask> RedisStreamStateManager::scanTimeoutTasks(std::chrono::milliseconds timeout)
{
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const auto cutoff = static_cast<double>(now_ms - timeout.count());

    const std::string zset_key = buildTaskTimestampZSetKey();
    const auto candidate_keys = _cacheManager.zsetRangeByScore(zset_key, 0, cutoff);

    std::vector<StreamTask> expired_tasks;

    for (const auto& task_key : candidate_keys)
    {
        if (!_cacheManager.zsetRem(zset_key, task_key))
        {
            continue;
        }

        auto task_opt = getTaskByKey(task_key);
        if (!task_opt)
        {
            continue;
        }

        const auto last_active_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            task_opt->last_active_time.time_since_epoch()
        ).count();

        if (now_ms - last_active_ms < timeout.count())
        {
            bestEffort(_cacheManager.zsetAdd(zset_key, static_cast<double>(last_active_ms), task_key), "zsetRollback",
                       task_key);
            continue;
        }

        // 确认真正过期
        deregisterTask(task_opt->stream_name, task_opt->client_id);
        expired_tasks.push_back(std::move(*task_opt));
    }
    return expired_tasks;
}

// 统计信息
size_t RedisStreamStateManager::getActivePublisherCount() const
{
    return _cacheManager.setCard(buildActivePublishersKey());
}

size_t RedisStreamStateManager::getActivePlayerCount() const
{
    auto fields = _cacheManager.hashGetAll(buildGlobalPlayerCountKey());
    if (auto it = fields.find("total"); it != fields.end())
    {
        try
        {
            auto val = std::stoull(it->second);
            return static_cast<size_t>(val);
        }
        catch (...)
        {
        }
    }

    return 0;
}

size_t RedisStreamStateManager::getPlayerCount(const std::string& stream_name) const
{
    return _cacheManager.setCard(buildPlayerSetKey(stream_name));
}

// 健康检查
bool RedisStreamStateManager::isHealthy() const
{
    return _cacheManager.ping();
}

//索引管理（明确 pub_key 存储逻辑） 唯一宿主
bool RedisStreamStateManager::registerPublisherIndices(const StreamTask& task) const
{
    const std::string pub_lock_key = buildPublisherKey(task.stream_name);
    const std::string member_index_key = "stream:members:" + task.stream_name;
    const std::string active_pub_key = buildActivePublishersKey();

    //唯一性校验：确保推流位没被别人抢占
    if (const auto current_task_opt = getTaskByKey(pub_lock_key))
    {
        if (current_task_opt->client_id != task.client_id)
        {
            LOG_WARN("Conflict: Stream " + task.stream_name + " already has a publisher");
            return false;
        }
    }

    //写入推流详情与索引
    try
    {
        auto pipe = _cacheManager.createPipeline();

        auto task_data = serializeTask(task);

        pipe.hset(pub_lock_key, task_data.begin(), task_data.end());
        pipe.sadd(member_index_key, task.client_id);
        pipe.sadd(active_pub_key, task.stream_name);

        pipe.exec();
    }
    catch (const sw::redis::Error& err)
    {
        LOG_ERROR("registerPublisherIndices pipeline failed: " + std::string(err.what()));
        return false;
    }
    return true;
}

//注册播放者索引：定死“寄生者”语义
bool RedisStreamStateManager::registerPlayerIndices(const StreamTask& task) const
{
    const std::string member_index_key = "stream:members:" + task.stream_name;
    const std::string global_key = buildGlobalPlayerCountKey();

    if (_cacheManager.setAdd(member_index_key, task.client_id))
    {
        bestEffort(_cacheManager.hashIncrBy(global_key, "total", 1), "hincrby", global_key);
        return true;
    }

    return false;
}

//联动清理原子入口
void RedisStreamStateManager::deregisterAllMembers(const std::string& stream_name)
{
    const std::string member_key = "stream:members:" + stream_name;
    auto clientIds = getStreamClientIds(stream_name);

    if (clientIds.empty())
    {
        bestEffort(_cacheManager.keyDel(member_key), "keyDel", member_key);
        return;
    }

    std::vector<TaskIdentifier> tasks;
    for (const auto& cid : clientIds)
    {
        if (const auto task_opt = getTask(stream_name, cid))
        {
            tasks.push_back({stream_name, cid, task_opt->type});
        }
    }

    deregisterTasksBatch(tasks);

    // 兜底清理成员索引
    bestEffort(_cacheManager.keyDel(member_key), "keyDel", member_key);
    LOG_INFO("Cleanup: Stream " + stream_name + " all members cleared.");
}

size_t RedisStreamStateManager::deregisterTasksBatch(const std::vector<TaskIdentifier>& tasks)
{
    if (tasks.empty())return 0;

    const std::string global_key = buildGlobalPlayerCountKey();
    const std::string active_pub_key = buildActivePublishersKey();

    try
    {
        auto pipe = _cacheManager.createPipeline();

        for (const auto& task : tasks)
        {
            std::string detail_key = buildTaskKey(task.streamName, task.clientId);
            std::string member_key = "stream:members:" + task.streamName;

            pipe.del(detail_key);
            pipe.srem(member_key, task.clientId);

            if (task.type == StreamType::PLAYER)
            {
                pipe.hincrby(global_key, "total", -1);
            }
            else if (task.type == StreamType::PUBLISHER)
            {
                pipe.del(buildPublisherKey(task.streamName));
                pipe.srem(active_pub_key, task.streamName);
            }
        }

        pipe.exec();
    }
    catch (const sw::redis::Error& err)
    {
        LOG_ERROR("deregisterTasksBatch pipeline failed: " + std::string(err.what()));
        return 0;
    }

    return tasks.size();
}

void RedisStreamStateManager::deregisterPublisherIndices(const std::string& stream_name) const
{
    const auto pub_key = buildPublisherKey(stream_name);

    bestEffort(_cacheManager.setRem(buildActivePublishersKey(), stream_name), "setRem", stream_name);

    bestEffort(_cacheManager.keyDel(pub_key), "keyDel", pub_key);
}

void RedisStreamStateManager::deregisterPlayerIndices(const std::string& stream_name,
                                                      const std::string& client_id) const
{
    const std::string player_set_key = buildPlayerSetKey(stream_name);
    const std::string global_key = buildGlobalPlayerCountKey();

    if (_cacheManager.setRem(player_set_key, client_id))
    {
        long long new_val = _cacheManager.hashIncrBy(global_key, "total", -1);

        if (new_val < 0)
        {
            bestEffort(_cacheManager.hashSet(global_key, {{"total", "0"}}), "resetNegativeCounter", global_key);
        }
    }
    else
    {
        LOG_DEBUG("Player "+client_id+" not in set, skipping decrement");
    }
}

// Key 构造器
std::string RedisStreamStateManager::buildTaskKey(const std::string& stream_name, const std::string& client_id)
{
    return "task:" + stream_name + ":" + client_id;
}

std::string RedisStreamStateManager::buildPublisherKey(const std::string& stream_name)
{
    return "pub:" + stream_name;
}

std::string RedisStreamStateManager::buildPlayerSetKey(const std::string& stream_name)
{
    return "players:" + stream_name;
}

std::string RedisStreamStateManager::buildStreamMetaKey(const std::string& stream_name)
{
    return "meta:" + stream_name;
}

std::string RedisStreamStateManager::buildActivePublishersKey()
{
    return "active_pubs";
}

std::string RedisStreamStateManager::buildGlobalPlayerCountKey()
{
    return "global_players";
}

std::string RedisStreamStateManager::buildTaskTimestampZSetKey()
{
    return "task_timestamps";
}

// 任务加载
std::optional<StreamTask> RedisStreamStateManager::getTaskByKey(const std::string& task_key) const
{
    auto fields = _cacheManager.hashGetAll(task_key);
    if (fields.empty())
    {
        return std::nullopt;
    }

    return deserializeTask(fields);
}
