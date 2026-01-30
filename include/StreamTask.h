//
// Created by wxx on 12/14/25.
//

#ifndef STREAMGATE_STREAMTASK_H
#define STREAMGATE_STREAMTASK_H
#include <string>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include "ZlmHookCommon.h"

enum class StreamType
{
    PUBLISHER, //推流
    PLAYER //拉流
};

enum class StreamState
{
    INITIALIZING, // 刚创建，等待连接
    ACTIVE, // 正常推/拉流中
    INACTIVE, // 长时间无数据（可能卡顿）
    ERROR, // 发生错误
    CLOSED // 已关闭
};

struct StreamTask
{
    //唯一标识
    uint64_t task_id{0}; //全局唯一任务ID
    std::string stream_name; //流名称
    std::string client_id; //客户端唯一ID

    //类型与状态
    StreamType type{StreamType::PLAYER};
    StreamState state{StreamState::INITIALIZING};
    StreamProtocol protocol{StreamProtocol::Unknown};

    //位置信息
    std::string server_ip; //当前所在流媒体服务器IP
    int server_port{0}; //当前所在流媒体服务器端口号

    //时间戳
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point last_active_time; //最后一次收到数据/心跳时间

    //业务上下文
    std::string user_id; //关联业务用户ID
    std::string auth_token; //认证token
    std::optional<std::string> region; //用户地区

    //资源与统计
    std::shared_ptr<std::atomic<uint64_t>> bandwidth_kbps{nullptr}; //当前带宽（kbps），支持多线程更新
    std::shared_ptr<std::atomic<uint32_t>> player_count{nullptr}; //仅对PUBLISHER有效：当前拉流人数

    //扩展字段
    bool need_transcode{false};
    bool need_record{false};
    std::string transcoding_profile; //转码模板

    //辅助方法
    void update_active()
    {
        last_active_time = std::chrono::system_clock::now();
    }

    [[nodiscard]] bool is_timeout(std::chrono::seconds timeout_sec = std::chrono::seconds(60)) const
    {
        auto now = std::chrono::system_clock::now();
        return (now - last_active_time) > timeout_sec;
    }

    [[nodiscard]] uint64_t duration_seconds() const
    {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    }
};

//StreamProtocol
inline std::string toString(StreamProtocol proto)
{
    switch (proto)
    {
    case StreamProtocol::RTMP: return "rtmp";
    case StreamProtocol::RTSP: return "rtsp";
    case StreamProtocol::HLS: return "hls";
    case StreamProtocol::HTTP_FLV: return "http-flv";
    case StreamProtocol::HTTP_TS: return "http-ts";
    case StreamProtocol::HTTP_FMP4: return "http-fmp4";
    case StreamProtocol::WebRTC: return "webrtc";
    case StreamProtocol::SRT: return "srt";
    default: return "unknown";
    }
}

inline StreamProtocol parseProtocol(std::string s)
{
    std::ranges::transform(s, s.begin(), ::tolower);

    if (s == "rtmp") return StreamProtocol::RTMP;
    if (s == "rtsp") return StreamProtocol::RTSP;
    if (s == "hls") return StreamProtocol::HLS;
    if (s == "http-flv") return StreamProtocol::HTTP_FLV;
    if (s == "http-ts") return StreamProtocol::HTTP_TS;
    if (s == "http-fmp4") return StreamProtocol::HTTP_FMP4;
    if (s == "webrtc") return StreamProtocol::WebRTC;
    if (s == "srt") return StreamProtocol::SRT;
    return StreamProtocol::Unknown;
}

// StreamState
inline std::string toString(StreamState state)
{
    switch (state)
    {
    case StreamState::INITIALIZING: return "initializing";
    case StreamState::ACTIVE: return "active";
    case StreamState::INACTIVE: return "inactive";
    case StreamState::ERROR: return "error";
    case StreamState::CLOSED: return "closed";
    default: return "unknown";
    }
}

inline StreamState parseState(const std::string& s)

{
    if (s == "initializing") return StreamState::INITIALIZING;
    if (s == "active") return StreamState::ACTIVE;
    if (s == "inactive") return StreamState::INACTIVE;
    if (s == "error") return StreamState::ERROR;
    if (s == "closed") return StreamState::CLOSED;
    return StreamState::INITIALIZING; // 默认回退到初始态（比 UNKNOWN 更合理）
}

//StreamType
inline std::string toString(StreamType type)
{
    return (type == StreamType::PUBLISHER) ? "publisher" : "player";
}

inline StreamType parseType(const std::string& s)
{
    if (s == "publisher") return StreamType::PUBLISHER;
    return StreamType::PLAYER;
}
#endif //STREAMGATE_STREAMTASK_H
