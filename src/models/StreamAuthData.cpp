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
        return nlohmann::json(*this).dump();
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
        auto j = nlohmann::json::parse(data);
        auto result = j.get<StreamAuthData>();

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
