//
// Created by X on 2025/11/17.
//
#include "DBManager.h"
#include "ConfigLoader.h"
#include "Logger.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <nlohmann/json.hpp>

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

void ThreadPool::stop_and_wait()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }

    condition.notify_all();

    for (std::thread& worker : workers)
        if (worker.joinable())
            worker.join();
}


ThreadPool::~ThreadPool()
{
    stop_and_wait();
}

DBManager::DBManager() : _dirver(sql::mariadb::get_driver_instance()),
                         _threadPool(
                             std::make_unique<ThreadPool>(ConfigLoader::instance().getInt("SERVER_MAX_THREADS", 4)))
{
}

DBManager::~DBManager()
{
    if (_threadPool)
    {
        LOG_INFO("DBManager: Starting graceful ThreadPool shutdown...");

        _threadPool->stop_and_wait();

        LOG_INFO("DBManager: ThreadPool successfully stopped and joined.");
    }

    {
        std::lock_guard<std::mutex> lock(_connectionMutex);

        LOG_INFO("DBManager: Closing all database connections in the pool...");

        while (! _connectionPool.empty())
        {
            std::unique_ptr<sql::Connection> conn = std::move(_connectionPool.front());
            _connectionPool.pop();

            if (conn)
            {
                try
                {
                    conn->close();
                }
                catch (const sql::SQLException& e)
                {
                    LOG_WARN("DBManager: Error closing DB connection: " +std::string(e.what()));
                }
            }
        }
    }

    LOG_INFO("DBManager: All MariaDB connections closed.");
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
            LOG_ERROR("DBManager getConnection SQL Error (Stale): "+std::string(e.what())+". Dropping connection.");
            continue;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("DBManager getConnection General Error: "+std::string(e.what())+ ". Dropping connection.");
            continue;
        }
        catch (...)
        {
            LOG_ERROR("DBManager getConnection Unknown Error. Dropping connection.");
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
    LOG_INFO("DBManager: Initializing MariaDB connection pool...");
    std::vector<std::unique_ptr<sql::Connection>> temp_pool;
    ConfigLoader& config = ConfigLoader::instance();
    try
    {
        const std::string host = ConfigLoader::instance().getString("DB_HOST");
        const int port = ConfigLoader::instance().getInt("DB_PORT");
        const std::string user = ConfigLoader::instance().getString("DB_USER");
        const std::string pass = ConfigLoader::instance().getString("DB_PASS");
        const std::string name = ConfigLoader::instance().getString("DB_NAME");

        const size_t initial_pool_size = config.getInt("DB_POOL_SIZE", 5);

        const std::string url = "jdbc:mariadb://" + host + ":" + std::to_string(port);

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

        LOG_INFO(
            "DBManager: MariaDB connection pool initialized. (Host: "+host+ ", Pool Size: "+ std::to_string(
                initial_pool_size)+")");
    }
    catch (const sql::SQLException& e)
    {
        LOG_FATAL("DBManager FATAL SQL ERROR during connection: "+std::string(e.what()));
        throw;
    }
    catch (const std::exception& e)
    {
        LOG_FATAL("DBManager FATAL ERROR: " +std::string(e.what()));
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
            LOG_ERROR("DBManager Error: Failed to get connection from pool (exhausted).");
            return -1; // DB 错误
        }

        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->prepareStatement(
                "SELECT COUNT(id) AS cnt FROM stream_auth "
                "WHERE stream_key = ? AND client_id = ? AND auth_token = ? AND is_active = 1"
                ));

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
        std::string result_str = result == 1 ? "Success" : "Failed";
        LOG_INFO("DBManager: Sync check for stream " +streamName+ " completed. Result: "+result_str);
        releaseConnection(std::move(conn));
    }
    catch (const sql::SQLException& e)
    {
        LOG_ERROR(
            "DBManager SQL Query Error: "+std::string(e.what())+" (Stream: "+streamName+ "). Dropping connection.");
        result = -1; // DB 错误
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("DBManager General Error: " +std::string(e.what())+". Dropping connection.");
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

bool DBManager::insertAuthForTest(const std::string& stream, const std::string& client, const std::string& token)
{
    try
    {
        auto conn = getConnection();
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "REPLACE INTO stream_auth(stream_key, client_id, auth_token) VALUES(?, ?, ?)"
                ));

        stmt->setString(1, stream);
        stmt->setString(2, client);
        stmt->setString(3, token);
        stmt->executeUpdate();
        conn->commit();
        releaseConnection(std::move(conn));
        return true;
    }
    catch (const sql::SQLException& e)
    {
        std::cerr << "Insert test auth failed: " << e.what() << std::endl;
        return false;
    }
}

std::optional<StreamAuthData> DBManager::getAuthDataFromDB(const std::string& streamKey,
                                                           const std::string& clientId,
                                                           const std::string& authToken)
{
    std::unique_ptr<sql::Connection> conn(getConnection());
    if (! conn)
    {
        LOG_ERROR("DBManager: Failed to get database connection.");
        //return std::nullopt;
        throw std::runtime_error("Failed to get database connection");
    }

    const std::string sql = R"(
    SELECT
        client_id,
        is_active AS is_valid,
        '' AS metadata_json
    FROM stream_auth
    WHERE stream_key = ?
      AND client_id = ?
      AND auth_token = ?
      AND is_active = 1
)";

    try
    {
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));

        pstmt->setString(1, streamKey);
        pstmt->setString(2, clientId);
        pstmt->setString(3, authToken);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (! res->next())
        {
            LOG_INFO("DBManager: Auth data not found for stream " + streamKey);
            return std::nullopt;
        }

        StreamAuthData data;

        data.streamKey = streamKey;
        data.authToken = authToken;

        data.clientId = res->getString("client_id");
        data.isAuthorized = res->getBoolean("is_valid");

        data.metadata.clear();
        std::string metadateStr;
        if (! res->isNull("metadata_json"))
        {
             metadateStr = res->getString("metadata_json");

            try
            {
                nlohmann::json j = nlohmann::json::parse(metadateStr);
                if (j.is_object())
                {
                    for (auto& [k,v] : j.items())
                    {
                        if (v.is_string())
                        {
                            data.metadata[k] = v.get<std::string>();
                        }
                    }
                }
            }
            catch (...)
            {
            }
        }
        else
        {
            metadateStr = "{}";
        }

        if (res->isNull("metadata_json"))
        {
            data.metadata = {};
        }
        else
        {
            std::string metadataJson = res->getString("metadata_json").c_str();
        }

        if (data.clientId != clientId)
        {
            LOG_WARN("DBManager: Client ID mismatch for stream "+streamKey+ ". Authorization FAILED.");
            data.isAuthorized = false;
        }

        return data;
    }
    catch (sql::SQLException& e)
    {
        LOG_ERROR("DBManager SQL Exception: " +std::string(e.what())+" (Code: " +std::to_string(e.getErrorCode())+ ")");
        throw;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("DBManager General Exception: "+std::string(e.what()));
        throw;
    }
}