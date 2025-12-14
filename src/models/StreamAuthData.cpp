//
// Created by wxx on 12/9/25.
//
#include "StreamAuthData.h"
#include "Logger.h"
#include <boost/json/string_view.hpp>
#include <nlohmann/json.hpp>

std::string StreamAuthData::serialize() const
{
    try
    {
        nlohmann::json j;

        j["streamKey"] = streamKey;
        j["clientId"] = clientId;
        j["authToken"] = authToken;
        j["isAuthorized"] = isAuthorized;
        j["metadata"] = metadata;

        return j.dump();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("StreamAuthData serialization failed: " +std::string(e.what()));
        return "";
    }
}

std::optional<StreamAuthData> StreamAuthData::deserialize(const std::string& data)
{
    if (data.empty())
    {
        return std::nullopt;
    }

    try
    {
        auto j = nlohmann::json(data);
        StreamAuthData result;

        result.streamKey = j.value("streamKey", "");
        result.clientId = j.value("clientId", "");
        result.authToken = j.value("authToken", "");
        result.isAuthorized = j.value("isAuthorized", false);

        if (j.contains("metadata") && j["metadata"].is_object())
        {
            result.metadata = j["metadata"].get<std::map<std::string, std::string>>();
        }

        if (result.streamKey.empty())
        {
            return std::nullopt;
        }

        return result;
    }
    catch (const std::exception& e)
    {
        LOG_WARN("StreamAuthData deserialization failed: " +std::string(e.what()));
        return std::nullopt;
    }
}

