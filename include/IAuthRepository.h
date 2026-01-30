//
// Created by wxx on 12/11/25.
//

#ifndef STREAMGATE_IAUTHREPOSITORY_H
#define STREAMGATE_IAUTHREPOSITORY_H
#include "StreamAuthData.h"
#include <optional>
#include <string>

/**
 * @brief 批量鉴权请求结构
 */
struct AuthRequest
{
    std::string streamKey;
    std::string clientId;
    std::string authToken;
};

/**
 * @brief 鉴权数据存储接口 (IAuthRepository)
 * 职责：负责从持久化层 (DB) 或缓存层 (Redis) 获取鉴权信息。
 */
class IAuthRepository
{
public:
    virtual ~IAuthRepository() = default;

    /**
     * @brief 获取单个流的鉴权数据 (兼容旧接口)
     * @return 若数据不存在或后端故障返回 std::nullopt
     */
    virtual std::optional<StreamAuthData> getAuthData(const std::string& streamKey,
                                                      const std::string& clientId,
                                                      const std::string& authToken) =0;

    /**
     * @brief 存储后端健康检查
     * @return true: 后端 (DB/Redis) 响应正常; false: 连接断开或异常
     * @note 实现应保持轻量 (如 PING)
     */
    virtual bool isHealthy() =0;

    /**
     * @brief 批量获取鉴权数据 (性能优化项)
     * @param requests 请求列表
     * @return 与请求列表顺序一致的 optional 结果集
     */
    virtual std::vector<std::optional<StreamAuthData>> getAuthDataBatch(const std::vector<AuthRequest>& requests)
    {
        std::vector<std::optional<StreamAuthData>> results;

        results.reserve(requests.size());

        for (const auto& [streamKey, clientId, authToken] : requests)
        {
            results.push_back(getAuthData(streamKey, clientId, authToken));
        }
        return results;
    }

    /**
    * @brief [可选] 强制使缓存失效
     * 用于后台修改权限后，立即同步到流媒体节点
     *
     */
    virtual void invalidateCache(const std::string& /*streamKey*/)
    {
    }
};
#endif //STREAMGATE_IAUTHREPOSITORY_H
