//
// Created by X on 2025/11/16.
//

#ifndef STREAMGATE_AUTHMANAGER_H
#define STREAMGATE_AUTHMANAGER_H
#include <string>
#include <future>
#include <stdexcept>
#include <functional>
#include "boost/json.hpp"

using AuthCallback = std::function<void(int final_auth_code)>;

namespace AuthError
{
    constexpr int SUCCESS = 0;
    constexpr int CACHE_AUTH_FAIL = 100;
    constexpr int DB_AUTH_FAIL = 200;
    constexpr int DB_ERROR = 201;
    constexpr int RUNTIME_ERROR = 500;
}

struct HookParams
{
    std::string streamKey;
    std::string clientId;
    std::string authToken;
};

class AuthManager
{
public:
    static AuthManager& instance();

    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;

    void performCheckAsync(const HookParams& params, AuthCallback callback);

private:
    AuthManager();
    ~AuthManager() = default;

    static bool parseBody(const std::string& body,
                          std::string& streamName,
                          std::string& clientId,
                          std::string& authToken);
};

#endif //STREAMGATE_AUTHMANAGER_H