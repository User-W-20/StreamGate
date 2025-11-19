//
// Created by X on 2025/11/16.
//
#include "AuthManager.h"
#include "DBManager.h"
#include "Poco/JSON/Parser.h"
#include "Poco/Dynamic/Var.h"
#include "Poco/Exception.h"
#include <iostream>
#include <sstream>

using namespace Poco;

AuthManager::AuthManager()
{
    std::cout << "[AuthManager] Initializing database and Redis connections..." << std::endl;
}

bool AuthManager::parseBody(const std::string& body, std::string& streamName, std::string& clientId)
{
    try
    {
        Poco::JSON::Parser parser;
        Dynamic::Var result = parser.parse(body);
        Poco::JSON::Object::Ptr root = result.extract<Poco::JSON::Object::Ptr>();

        std::string action = root->get("action").toString();
        if (action != "on_publish")
        {
            std::cerr << "[AuthManager ERROR] Received non-publish action: " << action << std::endl;
            return false;
        }
        std::string app = root->get("app").toString();
        std::string stream = root->get("stream").toString();

        streamName = app + "/" + stream;

        clientId = root->get("client_id").toString();

        std::cout << "[AuthManager] Hook Parsed - Stream: " << streamName << ", ClientID: " << clientId << std::endl;

        return true;
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "[AuthManager ERROR] JSON parsing failed: " << e.displayText() << std::endl;
        return false;
    }
}

int AuthManager::performDbCheck(const std::string& streamName, const std::string& clientId)
{
    bool is_auth_ok = DBManager::instance().checkPublishAuth(streamName, clientId);

    if (is_auth_ok)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}


int AuthManager::checkHook(const std::string& body)
{
    std::string streamName;
    std::string clientId;
    int result = 0;

    if (! parseBody(body, streamName, clientId))
    {
        std::cout << "[AuthManager] DENIED (Invalid Hook Body/Action)." << std::endl;
        return 2;
    }

    result = performDbCheck(streamName, clientId);

    if (result == 0)
    {
        std::cout << "[AuthManager] ALLOWED." << std::endl;
    }
    else
    {
        std::cout << "[AuthManager] DENIED (DB Check Failed, Code: " << result << ")." << std::endl;
    }

    return result;
}