//
// Created by wxx on 2025/12/21.
//
#include "NodeConfig.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>

#include "Logger.h"

/**
 * @brief 手动拷贝构造函数
 * 解决 std::atomic 无法直接拷贝的问题
 */
NodeConfig::NodeConfig(const NodeConfig& other)
    : rtmp_srt(other.rtmp_srt),
      http_hls(other.http_hls),
      webrtc(other.webrtc)
{
    _rr_rtmp.store(other._rr_rtmp.load(std::memory_order_relaxed));
    _rr_http.store(other._rr_http.load(std::memory_order_relaxed));
    _rr_webrtc.store(other._rr_webrtc.load(std::memory_order_relaxed));
}

/**
 * @brief 手动赋值运算符
 */
NodeConfig& NodeConfig::operator=(const NodeConfig& other)
{
    if (this != &other)
    {
        rtmp_srt = other.rtmp_srt;
        http_hls = other.http_hls;
        webrtc = other.webrtc;
        _rr_rtmp.store(other._rr_rtmp.load(std::memory_order_relaxed));
        _rr_http.store(other._rr_http.load(std::memory_order_relaxed));
        _rr_webrtc.store(other._rr_webrtc.load(std::memory_order_relaxed));
    }
    return *this;
}

/**
 * @brief 工业级防御性 JSON 字段填充
 * 特性：高性能 get_to、显式 null 检查、Key 级别异常追踪
 */
template <typename T>
void NodeConfig::fill_if_present(const json& j, const std::string& key, T& target)
{
    try
    {
        if (j.contains(key))
        {
            const auto& val = j.at(key);
            if (val.is_null())
            {
                LOG_WARN("NodeConfig: Key '" + key + "' is null, using default.");
                return;
            }
            // 直接反序列化到目标地址，避免临时中间变量拷贝
            val.get_to(target);
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        // 捕获 JSON 类型不匹配等详细错误
        LOG_ERROR("NodeConfig: Type mismatch or format error at key '" + key + "': " + e.what());
        throw std::runtime_error("Config parse error at " + key);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("NodeConfig: Unexpected error parsing key '" + key + "': " + e.what());
        throw;
    }
}

/**
 * @brief 根据分类获取后端节点列表（只读引用）
 */
const std::vector<NodeEndpoint>& NodeConfig::getEndpointsByCat(Category cat) const
{
    switch (cat)
    {
    case Category::RTMP_SRT:
        return rtmp_srt;

    case Category::HTTP_HLS:
        return http_hls;

    case Category::WEBRTC:
        return webrtc;

    case Category::UNKNOWN:
    default:
        {
            //使用静态空容器作为兜底。
            // 这样即使分类错误，返回的引用依然指向一个合法的、生命周期为全局的空对象。
            static constexpr std::vector<NodeEndpoint> s_empty_vector;
            return s_empty_vector;
        }
    }
}

NodeEndpoint NodeConfig::getRoundRobinEndpoint(Category cat) const
{
    const auto& endpoints = getEndpointsByCat(cat);
    const size_t size = endpoints.size();

    if (size == 0)
    {
        LOG_ERROR("NodeConfig: Round-robin failed, empty endpoints for category.");
        throw std::runtime_error("No endpoints available for category");
    }

    std::atomic<size_t>* counter = nullptr;
    switch (cat)
    {
    case Category::RTMP_SRT: counter = &_rr_rtmp;
        break;
    case Category::HTTP_HLS: counter = &_rr_http;
        break;
    case Category::WEBRTC: counter = &_rr_webrtc;
        break;
    default: throw std::runtime_error("Invalid category");
    }

    size_t idx = counter->fetch_add(1, std::memory_order_relaxed) % size;
    return endpoints[idx];
}

NodeConfig::Category NodeConfig::stringToCategory(const std::string& category)
{
    static const std::map<std::string, Category> mapping = {
        {"rtmp", Category::RTMP_SRT}, {"srt", Category::RTMP_SRT}, {"rtmp_srt", Category::RTMP_SRT},
        {"http", Category::HTTP_HLS}, {"hls", Category::HTTP_HLS}, {"http_hls", Category::HTTP_HLS},
        {"webrtc", Category::WEBRTC}
    };

    std::string norm = category;
    std::ranges::transform(norm, norm.begin(), ::tolower);

    auto it = mapping.find(norm);
    return (it != mapping.end()) ? it->second : Category::UNKNOWN;
}

/**
 * @brief 线程安全的随机调度
 */
NodeEndpoint NodeConfig::getRandomEndpoint(Category cat) const
{
    // 获取引用（零拷贝）
    const auto& endpoints = getEndpointsByCat(cat);
    const size_t size = endpoints.size();

    // 严谨性检查：防止 size 为 0 导致随机数分布器报错
    if (size == 0)
    {
        LOG_ERROR("NodeConfig: Random access failed, empty endpoints.");
        throw std::runtime_error("No endpoints available for the specified category.");
    }

    // 线程局部的随机数引擎：
    // 每个线程拥有独立的状态，既保证了随机质量，又避免了加锁开销（比 rand() 更快且线程安全）
    thread_local std::mt19937 gen(std::random_device{}());

    // 分布范围是 [0, size - 1]
    std::uniform_int_distribution<size_t> dist(0, size - 1);

    return endpoints[dist(gen)];
}

NodeConfig NodeConfig::fromJsonFile(const std::string& filepath, const ValidationOptions& opts)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        throw std::runtime_error("NodeConfig: Could not open file at " + filepath);
    }

    json j;
    try
    {
        file >> j;
    }
    catch (const json::parse_error& e)
    {
        LOG_ERROR("NodeConfig: JSON Syntax error in " + filepath + " at byte " + std::to_string(e.byte));
        throw;
    }

    NodeConfig config;
    //填充数据
    fill_if_present(j, "rtmp_srt", config.rtmp_srt);
    fill_if_present(j, "http_hls", config.http_hls);
    fill_if_present(j, "webrtc", config.webrtc);

    // 业务验证逻辑 (Validation Logic)
    auto validate_group = [&](const std::vector<NodeEndpoint>& endpoints, const std::string& name)
    {
        // 检查空列表
        if (!opts.allow_empty_endpoints && endpoints.empty())
        {
            throw std::runtime_error("NodeConfig Validation: Category '" + name + "' cannot be empty.");
        }

        for (size_t i = 0; i < endpoints.size(); ++i)
        {
            const auto& ep = endpoints[i];
            std::string context = name + "[" + std::to_string(i) + "](" + ep.host + ")";

            // 检查 Host 合法性
            if (opts.require_valid_hosts && (ep.host.empty() || ep.host == "0.0.0.0"))
            {
                throw std::runtime_error("NodeConfig Validation: Invalid or unsafe host in " + context);
            }

            // 检查 Port 合法性
            if (opts.strict_port_rang && (ep.port <= 0 || ep.port > 65535))
            {
                throw std::runtime_error("NodeConfig Validation: Port out of range in " + context);
            }

            // 检查常见的配置失误：比如 RTMP 默认不该是 80 端口
            if (name == "rtmp_srt" && ep.port == 80)
            {
                LOG_WARN("NodeConfig: Potential port misconfiguration. " + context + " is using port 80 for RTMP/SRT.");
            }
        }
    };

    // 执行三组校验
    validate_group(config.rtmp_srt, "rtmp_srt");
    validate_group(config.http_hls, "http_hls");
    validate_group(config.webrtc, "webrtc");

    LOG_INFO("NodeConfig: Successfully loaded and validated " +
        std::to_string(config.rtmp_srt.size() + config.http_hls.size() + config.webrtc.size()) +
        " endpoints.");

    return config;
}
