//
// Created by X on 2025/11/16.
//

#ifndef STREAMGATE_AUTHMANAGER_H
#define STREAMGATE_AUTHMANAGER_H
#include <string>

class AuthManager
{
public:
    AuthManager();

    int checkHook(const std::string& body);

private:
    int performDbCheck(const std::string& body);
};

#endif //STREAMGATE_AUTHMANAGER_H