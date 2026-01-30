//
// Created by wxx on 12/16/25.
//
#include "StreamTaskSerializer.h"
#include "EnumToString.h"
#include <chrono>

std::map<std::string, std::string> StreamTaskSerializer::serialize(const StreamTask& task)
{
    std::map<std::string, std::string> fields;

    fields["task_id"] = std::to_string(task.task_id);
    fields["stream_name"] = task.stream_name;
    fields["client_id"] = task.client_id;
    fields["type"] = EnumToString::to_string(task.type);
    fields["state"] = EnumToString::to_string(task.state);
    fields["protocol"] = EnumToString::to_string(task.protocol);
    fields["server_ip"] = task.server_ip;
    fields["server_port"] = std::to_string(task.server_port);
    fields["start_time"] = time_point_to_string(task.start_time);
    fields["last_active_time"] = time_point_to_string(task.last_active_time);
    fields["user_id"] = task.user_id;
    fields["auth_token"] = task.auth_token;
    fields["bandwidth_kbps"] = std::to_string(task.bandwidth_kbps->load());
    fields["player_count"] = std::to_string(task.player_count->load());
    fields["need_transcode"] = task.need_transcode ? "1" : "0";
    fields["need_record"] = task.need_record ? "1" : "0";
    fields["transcoding_profile"] = task.transcoding_profile;

    fields["region"] = task.region.has_value() ? task.region.value() : "";

    return fields;
}

std::optional<StreamTask> StreamTaskSerializer::deserialize(const std::map<std::string, std::string>& fields)
{
    try
    {
        StreamTask task;

        task.task_id = std::stoull(get_field(fields, "task_id", "0"));
        task.stream_name = get_field(fields, "stream_name");
        task.client_id = get_field(fields, "client_id");

        task.type = EnumToString::from_string(StreamType{}, get_field(fields, "type"));
        task.state = EnumToString::from_string(StreamState{}, get_field(fields, "state"));
        task.protocol = EnumToString::from_string(StreamProtocol{}, get_field(fields, "protocol"));

        task.server_ip = get_field(fields, "server_ip");
        task.server_port = std::stoi(get_field(fields, "server_port", "0"));

        task.start_time = string_to_time_point(get_field(fields, "start_time"));
        task.last_active_time = string_to_time_point(get_field(fields, "last_active_time"));

        task.user_id = get_field(fields, "user_id");
        task.auth_token = get_field(fields, "auth_token");

        task.bandwidth_kbps = std::make_shared<std::atomic<uint64_t>>(
            std::stoull(get_field(fields, "bandwidth_kbps", "0")));
        task.player_count = std::make_shared<std::atomic<uint32_t>>(
            std::stoul(get_field(fields, "player_count", "0")));

        task.need_transcode = get_field(fields, "need_transcode", "0") == "1";
        task.need_record = get_field(fields, "need_record", "0") == "1";
        task.transcoding_profile = get_field(fields, "transcoding_profile");

        if (auto region = get_field(fields, "region"); !region.empty()) task.region = region;

        return std::optional<StreamTask>{task};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::string StreamTaskSerializer::time_point_to_string(const std::chrono::system_clock::time_point& tp)
{
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    return std::to_string(seconds);
}

std::chrono::system_clock::time_point StreamTaskSerializer::string_to_time_point(const std::string& s)
{
    try
    {
        long long seconds = std::stoll(s);
        return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
    }
    catch (...)
    {
        return {};
    }
}

std::string StreamTaskSerializer::get_field(const std::map<std::string, std::string>& fields, const std::string& key,
                                            const std::string& default_val)
{
    auto it = fields.find(key);
    if (it != fields.end())
    {
        return it->second;
    }
    return default_val;
}
