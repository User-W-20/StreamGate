//
// Created by wxx on 2025/12/22.
//
#include "ZlmHookCommon.h"

HookAction ZlmHookRequest::parse_action(std::string_view action_str)
{
    static const std::unordered_map<std::string_view, HookAction> m = {
        {"on_publish", HookAction::Publish},
        {"on_play", HookAction::Play},
        {"on_publish_done", HookAction::PublishDone},
        {"on_play_done", HookAction::PlayDone},
        {"on_stream_none_reader", HookAction::StreamNoneReader}
    };

    const auto it = m.find(action_str);
    return (it != m.end()) ? it->second : HookAction::Unknown;
}

StreamProtocol ZlmHookRequest::parse_protocol(std::string_view schema_str)
{
    static const std::unordered_map<std::string_view, StreamProtocol> m = {
        {"rtmp", StreamProtocol::RTMP},
        {"http-flv", StreamProtocol::HTTP_FLV},
        {"hls", StreamProtocol::HLS},
        {"rtsp", StreamProtocol::RTSP},
        {"webrtc", StreamProtocol::WebRTC}
    };

    const auto it = m.find(schema_str);
    return (it != m.end()) ? it->second : StreamProtocol::Unknown;
}

std::optional<ZlmHookRequest> ZlmHookRequest::from_json(const json& j)
{
    try
    {
        ZlmHookRequest req;

        //核心拦截：如果没有 action，直接判定解析失败
        auto it_action = j.find("action");
        // if (it_action == j.end())[[unlikely]]
        // {
        //     return std::nullopt;
        // }

        req.action = (it_action != j.end()) ? parse_action(it_action->get<std::string>()) : HookAction::Unknown;

        //Schema 判定：优先取 schema，次选 protocol，默认 rtmp
        std::string schema_str = j.value("schema", j.value("protocol", "rtmp"));
        req.protocol = parse_protocol(schema_str);

        //基础字段取值 (使用 value 带有默认值的重载，安全且高效)
        req.app = j.value("app", "live");
        req.stream = j.value("stream", "");
        req.vhost = j.value("vhost", "__defaultVhost__");
        req.client_id = j.value("id", "");
        req.ip = j.value("ip", "");

        //Params 解析 (Best Effort 策略)
        if (auto it = j.find("params"); it != j.end() && it->is_string())
        {
            const auto& params_str = it->get_ref<const std::string&>();
            if (!params_str.empty())
            {
                try
                {
                    // 尝试作为 JSON 解析
                    json params_json = json::parse(params_str);
                    for (const auto& [key,value] : params_json.items())
                    {
                        if (value.is_string())
                        {
                            req.params[key] = value.get<std::string>();
                        }
                    }
                }
                catch (const json::parse_error&)
                {
                    // JSON 解析失败，回退到标准 URL 参数解析 (sg=1&type=test)
                    parse_url_params(params_str, req.params);
                }
            }
        }
        return req;
    }
    catch (const json::exception&)
    {
        [[unlikely]] return std::nullopt;
    }
}

void ZlmHookRequest::parse_url_params(const std::string& query, std::map<std::string, std::string>& out)
{
    size_t pos = 0;
    while (pos < query.size())
    {
        size_t eq = query.find('=', pos);
        if (eq == std::string::npos)break;

        size_t amp = query.find('&', eq);
        if (amp == std::string::npos)amp = query.size();

        std::string key = query.substr(pos, eq - pos);
        std::string value = query.substr(eq + 1, amp - eq - 1);

        out[key] = value;

        pos = amp + 1;
    }
}

ZlmHookResponse HookDecision::to_response() const
{
    //成功分支
    if (outcome == Outcome::Allow) return {ZlmHookResult::SUCCESS, "success"};

    //延迟分支
    if (outcome == Outcome::Defer) return {ZlmHookResult::TIMEOUT, "processing"};

    //拒绝分支 (包含：Token错误、流不存在、身份过期等)
    if (reason.find("auth") != std::string::npos ||
        reason.find("Identity") != std::string::npos ||
        reason.find("not found") != std::string::npos)
    {
        return {ZlmHookResult::AUTH_DENIED, reason};
    }

    //真正地兜底：意料之外的错误
    return {ZlmHookResult::INTERNAL_ERROR, reason};
}
