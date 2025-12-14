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
#include "IAuthRepository.h"
#include "ThreadPool.h"

using AuthCallback = std::function<void(int final_auth_code)>;

namespace AuthError
{
    constexpr int SUCCESS = 0;
    constexpr int AUTH_DENIED = 403;
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
    AuthManager(std::unique_ptr<IAuthRepository> repository, ThreadPool& sharedPool);
    ~AuthManager() = default;

    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;

    void performCheckAsync(const HookParams& params, AuthCallback callback);

private:
    std::unique_ptr<IAuthRepository> _repository;

    ThreadPool& _sharedPool;

    static bool parseBody(const std::string& body,
                          std::string& streamName,
                          std::string& clientId,
                          std::string& authToken);
};

#endif //STREAMGATE_AUTHMANAGER_H