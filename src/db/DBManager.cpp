//
// Created by X on 2025/11/17.
//
#include "DBManager.h"
#include "ConfigLoader.h"
#include <iostream>
#include <stdexcept>
#include <chrono>

ThreadPool::ThreadPool(size_t threads) : stop(false)
{
    if (threads == 0)
        throw std::invalid_argument("Thread count must be greater than zero.");

    for (size_t i = 0; i < threads; ++i)
    {
        workers.emplace_back(
            [this]
            {
                for (;;)
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                                             [this]
                                             {
                                                 return this->stop || ! this->tasks.empty();
                                             }
                            );

                        if (this->stop && this->tasks.empty())
                        {
                            return;
                        }
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            }
            );
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }

    condition.notify_all();
    for (std::thread& worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}

DBManager::DBManager() : _dirver(sql::mariadb::get_driver_instance()),
                         _threadPool(
                             std::make_unique<ThreadPool>(ConfigLoader::instance().getInt("SERVER_MAX_THREADS", 4)))
{
}

DBManager& DBManager::instance()
{
    static DBManager instance;
    return instance;
}

std::unique_ptr<sql::Connection> DBManager::getConnection()
{
    std::unique_lock<std::mutex> lock(_connectionMutex);
    if (_connectionPool.empty())
    {
        throw std::runtime_error("DBManager: Connection pool exhausted.");
    }

    std::unique_ptr<sql::Connection> conn = std::move(_connectionPool.front());
    _connectionPool.pop();

    return conn;
}

void DBManager::releaseConnection(std::unique_ptr<sql::Connection> conn)
{
    if (conn)
    {
        std::unique_lock<std::mutex> lock(_connectionMutex);
        _connectionPool.push(std::move(conn));
    }
}

void DBManager::connect()
{
    std::cout << "DBManager: Initializing MariaDB connection pool..." << std::endl;

    try
    {
        const std::string host = ConfigLoader::instance().getString("DB_HOST");
        const int port = ConfigLoader::instance().getInt("DB_PORT");
        const std::string user = ConfigLoader::instance().getString("DB_USER");
        const std::string pass = ConfigLoader::instance().getString("DB_PASS");
        const std::string name = ConfigLoader::instance().getString("DB_NAME");

        const std::string url = "jdbc:mariadb://" + host + ":" + std::to_string(port);
        constexpr size_t initial_pool_size = 5;

        for (size_t i = 0; i < initial_pool_size; ++i)
        {
            std::unique_ptr<sql::Connection> conn(_dirver->connect(url, user, pass));
            conn->setSchema(name);

            std::unique_lock<std::mutex> lock(_connectionMutex);
            _connectionPool.push(std::move(conn));
        }

        std::cout << "DBManager: MariaDB connection pool initialized. (Host: " << host << ", Pool Size: " <<
            initial_pool_size << ")" << std::endl;
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "DBManager FATAL SQL ERROR during connection: " << e.what() << std::endl;
        throw;
    }
    catch (const std::exception& e)
    {
        std::cerr << "DBManager FATAL ERROR: " << e.what() << std::endl;
        throw;
    }
}

int DBManager::performSyncCheck(const std::string& streamName, const std::string& clientId)
{
    int result=1;
    std::unique_ptr<sql::Connection>conn;

    try
    {
        conn=getConnection();

        std::unique_ptr<sql::PreparedStatement>pstmt(conn->prepareStatement("SELECT COUNT(id) FROM streams WHERE name=? AND client_id=? AND status=1"));

        pstmt->setString(1,streamName);
        pstmt->setString(2,clientId);

        std::unique_ptr<sql::ResultSet>res(pstmt->executeQuery());

        if (res->next())
        {
            int count=res->getInt(1);
            if (count>0)
            {
                result=0;
            }
        }
        std::cout << "DBManager: Sync check for stream "<<streamName<<" completed. Result: " <<(result==0?"Success":"Failed")<<std::endl;
    }catch (const sql::SQLException&e)
    {
        std::cerr<<"DBManager SQL Query Error: " <<e.what()<<" (Stream: " <<streamName<<")" << std::endl;
        result=-1;
    }catch (const std::exception&e)
    {
        std::cerr << "DBManager General Error: " << e.what() << std::endl;
        result=-2;
    }

    releaseConnection(std::move(conn));
    return result;
}

std::future<int> DBManager::asyncCheckStream(const std::string& streamName, const std::string& clientId)
{
    return _threadPool->submit(
        &DBManager::performSyncCheck,
        this,
        streamName,
        clientId
        );
}

