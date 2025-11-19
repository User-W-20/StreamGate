//
// Created by X on 2025/11/16.
//

#ifndef STREAMGATE_AUTHMANAGER_H
#define STREAMGATE_AUTHMANAGER_H
#include <string>
#include "Poco/JSON/Object.h"

class AuthManager
{
public:
    AuthManager();

    int checkHook(const std::string& body);

private:
    bool parseBody(const std::string& body, std::string& streamName, std::string& clientId);

    int performDbCheck(const std::string& streamName, const std::string& clientId);
};

#endif //STREAMGATE_AUTHMANAGER_H