//
// Created by X on 2025/11/16.
//
#include "AuthManager.h"
#include "Poco/JSON/Parser.h"
#include "Poco/Dynamic/Var.h"
#include "Poco/Exception.h"
#include <iostream>

using namespace Poco;

AuthManager::AuthManager()
{
    std::cout << "[AuthManager] Initializing database and Redis connections..." << std::endl;
}

int AuthManager::performDbCheck(const std::string& body)
{
    try
    {
        JSON::Parser parser;
        Dynamic::Var result = parser.parse(body);

        Poco::JSON::Object::Ptr obj = result.extract<Poco::JSON::Object::Ptr>();

        std::string action = obj->getValue<std::string>("action");
        std::string stream = obj->getValue<std::string>("stream");
        std::string client_id = obj->getValue<std::string>("client_id");

        std::cout << "[Auth] Hook Received: Action=" << action
            << ", Stream=" << stream << ", ClientID=" << client_id << std::endl;

        if (action == "on_publish")
        {
            if (stream.find("deny") != std::string::npos)
            {
                std::cout << "[Auth] DENIED: Stream name contains 'deny'." << std::endl;
                return 1;
            }
        }

        std::cout << "[Auth] ALLOWED." << std::endl;
        return 0;
    }
    catch (const Exception& e)
    {
        std::cerr << "[ERROR] JSON Parsing/Runtime Error in AuthManager: " << e.displayText() << std::endl;
        return 1;
    }
}


int AuthManager::checkHook(const std::string& body)
{
    return performDbCheck(body);
}