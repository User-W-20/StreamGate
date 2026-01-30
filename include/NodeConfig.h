//
// Created by wxx on 2025/12/21.
//

#ifndef STREAMGATE_NODECONFIG_H
#define STREAMGATE_NODECONFIG_H
#include <string>
#include <vector>
#include <atomic>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct NodeEndpoint
{
    std::string host;
    int port;

    [[nodiscard]] bool isValid() const
    {
        return !host.empty() && port > 0 && port <= 65535;
    }

    [[nodiscard]] std::string toString() const
    {
        return host + ":" + std::to_string(port);
    }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NodeEndpoint, host, port)

class NodeConfig
{
public:
    enum class Category
    {
        RTMP_SRT,
        HTTP_HLS,
        WEBRTC,
        UNKNOWN
    };

    struct ValidationOptions
    {
        bool allow_empty_endpoints;
        bool strict_port_rang;
        bool require_valid_hosts;

        ValidationOptions()
            : allow_empty_endpoints(false),
              strict_port_rang(true),
              require_valid_hosts(true)
        {
        }
    };

    std::vector<NodeEndpoint> rtmp_srt;
    std::vector<NodeEndpoint> http_hls;
    std::vector<NodeEndpoint> webrtc;

    NodeConfig() = default;

    // 拷贝构造函数：由于含有 atomic，必须显式定义
    NodeConfig(const NodeConfig& other);
    NodeConfig& operator=(const NodeConfig& other);

    static NodeConfig fromJsonFile(const std::string& filepath, const ValidationOptions& opts = ValidationOptions());
    static Category stringToCategory(const std::string& category);

    NodeEndpoint getRoundRobinEndpoint(Category cat) const;
    NodeEndpoint getRandomEndpoint(Category cat) const;

private:
    ValidationOptions _validation_opts;

    // 使用原子变量确保多线程 Round-Robin 安全且无锁竞争
    mutable std::atomic<size_t> _rr_rtmp{0};
    mutable std::atomic<size_t> _rr_http{0};
    mutable std::atomic<size_t> _rr_webrtc{0};

    const std::vector<NodeEndpoint>& getEndpointsByCat(Category cat) const;

    template <typename T>
    static void fill_if_present(const json& j, const std::string& key, T& target);
};
#endif //STREAMGATE_NODECONFIG_H
