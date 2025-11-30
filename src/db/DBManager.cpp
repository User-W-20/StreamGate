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

    while (! _connectionPool.empty())
    {
        std::unique_ptr<sql::Connection> conn = std::move(_connectionPool.front());
        _connectionPool.pop();

        try
        {
            // 活性检查：尝试执行一个简单的查询
            std::unique_ptr<sql::Statement> stmt(conn->createStatement());
            stmt->execute("SELECT 1");

            // 检查成功，返回连接
            return conn;
        }
        catch (const sql::SQLException& e)
        {
            // 捕获 SQL 异常，说明连接失效。丢弃它，继续检查下一个。
            std::cerr << "DBManager getConnection SQL Error (Stale): " << e.what() << ". Dropping connection." <<
                std::endl;
            continue;
        }
        catch (const std::exception& e)
        {
            std::cerr << "DBManager getConnection General Error: " << e.what() << ". Dropping connection." << std::endl;
            continue;
        }
        catch (...)
        {
            std::cerr << "DBManager getConnection Unknown Error. Dropping connection." << std::endl;
            continue;
        }
    }

    return nullptr; // 连接池耗尽
}

void DBManager::releaseConnection(std::unique_ptr<sql::Connection> conn)
{
    if (! conn)
        return;

    try
    {
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        stmt->execute("SELECT 1");
    }
    catch (...)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(_connectionMutex);
    _connectionPool.push(std::move(conn));
}

void DBManager::connect()
{
    std::cout << "DBManager: Initializing MariaDB connection pool..." << std::endl;
    std::vector<std::unique_ptr<sql::Connection>> temp_pool;

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
            temp_pool.push_back(std::move(conn));
        }

        {
            std::unique_lock<std::mutex> lock(_connectionMutex);
            for (auto& conn_ptr : temp_pool)
            {
                _connectionPool.push(std::move(conn_ptr));
            }
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

int DBManager::performSyncCheck(const std::string& streamName,
                                const std::string& clientId,
                                const std::string& authToken)
{
    std::unique_ptr<sql::Connection> conn = nullptr;
    int result = 0; // 0 表示业务认证失败

    try
    {
        conn = getConnection();

        if (! conn)
        {
            std::cerr << "DBManager Error: Failed to get connection from pool (exhausted)." << std::endl;
            return -1; // DB 错误
        }

        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->prepareStatement(
                "SELECT COUNT(id) FROM stream_auth WHERE stream_key=? AND client_id=? AND auth_token=? AND is_active=1"));

        pstmt->setString(1, streamName);
        pstmt->setString(2, clientId);
        pstmt->setString(3, authToken);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (res->next())
        {
            int count = res->getInt(1);
            if (count > 0)
            {
                result = 1; // 业务认证成功
            }
        }
        std::cout << "DBManager: Sync check for stream " << streamName << " completed. Result: " << (
            result == 1 ? "Success" : "Failed") << std::endl;

        releaseConnection(std::move(conn));
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "DBManager SQL Query Error: " << e.what() << " (Stream: " << streamName <<
            "). Dropping connection." << std::endl;
        result = -1; // DB 错误
    }
    catch (const std::exception& e)
    {
        std::cerr << "DBManager General Error: " << e.what() << ". Dropping connection." << std::endl;
        result = -2; // 系统错误
    }

    return result;
}

std::future<int> DBManager::asyncCheckStream(const std::string& streamName,
                                             const std::string& clientId,
                                             const std::string& authToken)
{
    return _threadPool->submit(
        &DBManager::performSyncCheck,
        this,
        streamName,
        clientId,
        authToken
        );
}