//
// Created by wxx on 2025/12/22.
//

#ifndef STREAMGATE_ZLMHOOKCOMMON_H
#define STREAMGATE_ZLMHOOKCOMMON_H
#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <utility>
#include <map>

using json = nlohmann::json;

enum class HookAction
{
    Publish, PublishDone, Play, PlayDone, StreamNoneReader, StreamNotFound, Unknown
};

enum class StreamProtocol
{
    RTMP, HTTP_FLV, HLS, RTSP, WebRTC, SRT, HTTP_TS, HTTP_FMP4, Unknown
};

enum class ZlmHookResult
{
    SUCCESS = 0,
    AUTH_DENIED = 1,
    INVALID_FORMAT = 2,
    UNSUPPORTED_ACTION = 3,
    INTERNAL_ERROR = 4,
    TIMEOUT = 5,
    RESOURCE_NOT_READY = 6
};

struct ZlmHookRequest
{
    HookAction action;
    StreamProtocol protocol;

    std::string app;
    std::string stream;
    std::string vhost;
    std::string client_id;
    std::string ip;

    std::map<std::string, std::string> params;

    [[nodiscard]] std::string stream_key() const
    {
        return vhost + "/" + app + "/" + stream;
    }

    [[nodiscard]] std::string get_token() const
    {
        const auto it = params.find("token");
        return it != params.end() ? it->second : "";
    }

    static std::optional<ZlmHookRequest> from_json(const json& j);

private:
    static HookAction parse_action(std::string_view action_str);
    static StreamProtocol parse_protocol(std::string_view schema_str);
    static void parse_url_params(const std::string& query, std::map<std::string, std::string>& out);
};

struct ZlmHookResponse
{
    ZlmHookResult code;
    std::string message;

    ZlmHookResponse(ZlmHookResult c, std::string msg = "")
        : code(c),
          message(std::move(msg))
    {
    }
};

struct HookDecision
{
    enum class Outcome
    {
        Allow, Deny, Defer
    };

    Outcome outcome;
    std::string reason;

    static HookDecision allow()
    {
        return {Outcome::Allow, "success"};
    }

    static HookDecision deny(std::string reason)
    {
        return {Outcome::Deny, std::move(reason)};
    }

    [[nodiscard]] ZlmHookResponse to_response() const;
};
#endif //STREAMGATE_ZLMHOOKCOMMON_H
