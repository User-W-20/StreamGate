//
// Created by X on 2025/11/17.
//
#include "DBManager.h"
#include "ConfigLoader.h"
#include <iostream>
#include "Poco/Data/Statement.h"
#include "Poco/Exception.h"
#include "Poco/Data/MySQL/Connector.h"

using namespace Poco::Data;
using namespace Poco::Data::Keywords;
using Poco::Data::Statement;
using Poco::Data::MySQL::Connector;

DBManager& DBManager::instance()
{
    static DBManager instance;
    return instance;
}

DBManager::DBManager()
{
    MySQL::Connector::registerConnector();

    _connectionString = ConfigLoader::instance().getDBConnectionString();

    int minConn = 4;
    int maxConn = 16;

    try
    {
        _pPool = std::make_unique<SessionPool>(MySQL::Connector::KEY,
                                               _connectionString,
                                               minConn,
                                               maxConn);
        std::cout << "[DBManager] MySQL SessionPool initialized successfully. Conn String: [" << _connectionString <<
            "]" << std::endl;
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "[DBManager ERROR] Failed to initialize SessionPool: " << e.displayText() << std::endl;
        throw;
    }
}

bool DBManager::checkPublishAuth(const std::string& streamName, const std::string& clientId)
{
    try
    {
        Session session = _pPool->get();

        int isActive = 0;
        std::size_t count = 0;

        std::string sql = "SELECT is_active FROM stream_auth WHERE stream_key = ? AND auth_token = ?";

        const char* streamCStr = streamName.c_str();
        const char* clientCStr = clientId.c_str();

        Statement select(session);

        select << sql,
            into(isActive),
            use(streamCStr),
            use(clientCStr),
            limit(1);

        count = select.execute();

        if (count == 0)
        {
            std::cout << "[DB] Auth FAILED: No record found for Stream=" << streamName << std::endl;
            return false;
        }

        if (isActive == 1)
        {
            std::cout << "[DB] Auth SUCCESS: Stream is active and token matches." << std::endl;
            return true;
        }
        else
        {
            std::cout << "[DB] Auth FAILED: Record found but is_active=0." << std::endl;
            return false;
        }
    }
    catch (const Poco::Data::DataException& e)
    {
        std::cerr << "[DBManager ERROR] Data query failed (SQL/DB Issue): " << e.displayText() << std::endl;
        return false;
    }
    catch (const Poco::Exception& e)
    {
        std::cerr << "[DBManager ERROR] Database operation failed: " << e.displayText() << std::endl;
        return false;
    }
    return false;
}