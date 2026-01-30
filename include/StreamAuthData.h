//
// Created by wxx on 12/9/25.
//

#ifndef STREAMGATE_STREAMAUTHDATA_H
#define STREAMGATE_STREAMAUTHDATA_H
#include <string>
#include <optional>
#include <map>
#include <nlohmann/json.hpp>

struct StreamAuthData
{
    std::string streamKey;
    std::string clientId;
    std::string authToken;

    bool isAuthorized = false;

    long long expireTime = 0;

    std::map<std::string, std::string> metadata;

    // 序列化
    [[nodiscard]] std::string serialize() const;

    //反序列化
    static std::optional<StreamAuthData> deserialize(const std::string& data);

    [[nodiscard]] bool isValid() const
    {
        return !streamKey.empty() && isAuthorized;
    }
};

template <>
struct nlohmann::adl_serializer<StreamAuthData>
{
    //serialize: StreamAuthData->json
    static void to_json(json& j, const StreamAuthData& d)
    {
        j = json{
            {"streamKey", d.streamKey},
            {"clientId", d.clientId},
            {"authToken", d.authToken},
            {"isAuthorized", d.isAuthorized},
            {"metadata", d.metadata},
            {"expireTime", d.expireTime}
        };
    }

    //deserialize:json->StreamAuthData
    static StreamAuthData from_json(const json& j)
    {
        StreamAuthData d;
        d.streamKey = j.value("streamKey", "");
        d.clientId = j.value("clientId", "");
        d.authToken = j.value("authToken", "");
        d.isAuthorized = j.value("isAuthorized", false);
        if (j.contains("metadata") && j["metadata"].is_object())
        {
            d.metadata = j["metadata"].get<std::map<std::string, std::string>>();
        }

        d.expireTime=j.value("expireTime", static_cast<uint64_t>(0));

        return d;
    }
};
#endif //STREAMGATE_STREAMAUTHDATA_H
