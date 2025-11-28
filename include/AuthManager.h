//
// Created by X on 2025/11/16.
//

#ifndef STREAMGATE_AUTHMANAGER_H
#define STREAMGATE_AUTHMANAGER_H
#include <string>
#include <future>
#include <stdexcept>
#include "boost/json.hpp"

class AuthManager
{
public:
    static AuthManager& instance();

    static int checkHook(const std::string& body);

    bool authenticate(const std::string&streamKey,const std::string &clientId);

    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;

private:
    AuthManager();
    ~AuthManager() = default;

   static bool parseBody(const std::string& body, std::string& streamName, std::string& clientId);

   static int performCheck(const std::string& streamName, const std::string& clientId);
};

#endif //STREAMGATE_AUTHMANAGER_H