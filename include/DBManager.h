//
// Created by X on 2025/11/17.
//

#ifndef STREAMGATE_DBMANAGER_H
#define STREAMGATE_DBMANAGER_H
#include "Poco/Data/Session.h"
#include "Poco/Data/SessionPool.h"
#include "Poco/Data/MySQL/Connector.h"
#include <string>
#include <memory>

using namespace Poco::Data;

class DBManager
{
public:
    static DBManager& instance();

    bool checkPublishAuth(const std::string& streamName, const std::string& clientId);

    DBManager(const DBManager&) = delete;
    DBManager& operator=(const DBManager&) = delete;

private:
    DBManager();
    ~DBManager() = default;

private:
    std::unique_ptr<SessionPool> _pPool;

    std::string _connectionString;
};
#endif //STREAMGATE_DBMANAGER_H