//
// Created by wxx on 12/9/25.
//

#ifndef STREAMGATE_STREAMAUTHDATA_H
#define STREAMGATE_STREAMAUTHDATA_H
#include <string>
#include <optional>
#include <map>

struct StreamAuthData
{
    std::string streamKey;
    std::string clientId;
    std::string authToken;

    bool isAuthorized = false;

    std::map<std::string, std::string> metadata;

    // 序列化
    [[nodiscard]] std::string serialize() const;

    //反序列化
    static std::optional<StreamAuthData> deserialize(const std::string& data);

    [[nodiscard]] bool isValid() const
    {
        return ! streamKey.empty() && isAuthorized;
    }
};
#endif //STREAMGATE_STREAMAUTHDATA_H